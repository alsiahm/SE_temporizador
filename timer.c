/* timer.c -- Librería de temporización para el 68HC12 (placa ADAPT912DT60)

   Autores: Alba Hidalgo Martín, Xuan Sun, Juhi Kamal Chatani Mansukhani

   ATENCIÓN: esta librería SOLO ES VÁLIDA SI EL RELOJ E VA A 8MHz

   La documentación de las funciones públicas está en timer.h
*/

#include <sys/param.h>
#include <sys/ports.h>
#include <sys/interrupts.h>
#include <sys/locks.h>
#include "timer.h"

#if M6812_CPU_E_CLOCK != 8000000
# error "timer: código solo válido si el reloj E va a 8MHz"
#endif

#if (TIMER_NUM_CANALES < 1) || (TIMER_NUM_CANALES > 8)
# error "timer: TIMER_NUM_CANALES debe estar entre 1 y 8"
#endif

/* Registros y banderines utilizados
 *  TCNT:  contador del temporizador, se incrementa con cada tick
 *  TMSK2: factor de división (PR2:PR0), interrupción de desbordamiento (TOI)
 *  TFLG2: banderín de desbordamiento de TCNT (TOF)
 *  TCTL2: acción sobre el pin de los canales 0 a 3 (OMn, OLn)
 *  TCTL1: acción sobre el pin de los canales 4 a 7 (OMn, OLn)
 *  TIOS:  selecciona cada canal como comparador de salida (IOSn)
 *  TCn:   registro de comparación del canal n
 *  TMSK1: habilita la interrupción de cada canal (CnI)
 *  TFLG1: banderín de coincidencia de cada canal (CnF)
 */

/* Registro de comparación (16 bits) del canal n */
#define TIMER_TC(n) _IO_PORTS_W(M6812_TC0 + ((n) << 1))

/* Bit del canal n en TIOS, TMSK1 y TFLG1 */
#define TIMER_BIT(n) ((uint8_t)(1 << (n)))

/* Bits en TIOS, TMSK1 y TFLG1 de todos los canales que usa la librería */
#define TIMER_BITS_CANALES ((uint8_t)((1U << TIMER_NUM_CANALES) - 1))

/* Ciclos de reloj E mínimos entre que se programa un disparo y éste se
   produce. Debe ser mayor que lo que se tarda en entrar en la rutina de
   interrupción y reprogramar el canal: si no, la coincidencia se perdería
   y el disparo se retrasaría una vuelta entera de TCNT */
#define TIMER_MARGEN_CICLOS 512


/* Conversión de unidades */

/* Factor de división del temporizador (de 0 a 7) fijado en timer_init */
static uint8_t factor_tcm;

/* Ticks mínimos de un tramo de espera (TIMER_MARGEN_CICLOS en ticks) */
static uint16_t margen_ticks;

/* Pasa de microsegundos a ticks según el factor de división.
   Si no cabe en 32 bits devuelve el máximo */
static uint32_t usg_2_ticks(uint32_t useg) {
  if (factor_tcm <= 3) {
    return useg << (3 - factor_tcm);
  }
  return useg >> (factor_tcm - 3);
}

/* Pasa de ticks a microsegundos según el factor de división */
static uint32_t ticks_2_usg(uint32_t ticks) {
  if (factor_tcm <= 3)
    return ticks >> (3 - factor_tcm);
  return ticks << (factor_tcm - 3);
}


/*  Cuenta de tiempo  */

/* Vueltas completas que ha dado TCNT. Se modifica SOLO en vi_tov (y se
   pone a 0 en timer_init). Es volatile porque cambia en una interrupción. */
static volatile uint16_t numero_vueltas;

/* Valor de TCNT en el momento de timer_init: origen de tiempo.*/
static uint16_t tcnt_inicio;

/* Interrupción de desbordamiento: TCNT pasa de 0xFFFF a 0x0000 */
void __attribute__((interrupt)) vi_tov(void) {
  numero_vueltas++;
  /* Bajamos el banderín de desbordamiento */
  _io_ports[M6812_TFLG2] = M6812B_TOF;
}

uint32_t timer_get_ticks(void) {
  uint16_t vueltas;
  uint16_t contador;
  /* Se guarda y restaura la máscara de interrupciones (en lugar de
     lock/unlock) para poder invocarla con las interrupciones enmascaradas */
  unsigned short mascara = lock();

  vueltas = numero_vueltas;
  contador = _IO_PORTS_W(M6812_TCNT);
  /* Puede que TCNT haya desbordado y aún no se haya atendido vi_tov.
     En ese caso contamos la vuelta en la copia local (numero_vueltas la
     actualizará vi_tov) y volvemos a leer TCNT, ya que no sabemos si la
     lectura anterior fue antes o después del desbordamiento */
  if (_io_ports[M6812_TFLG2] & M6812B_TOF) {
    vueltas++;
    contador = _IO_PORTS_W(M6812_TCNT);
  }

  /* Es como un unlock solo si antes estaba unlocked. Habilita la interrupcion
  si estaban habilitadas */
  restore(mascara);

  /* 16 bits izq: vueltas, 16 bits bajderos: TCNT. Se resta el origen */
  return (((uint32_t)vueltas << 16) | contador) - tcnt_inicio;
}

uint32_t timer_get_us(void) {
  return ticks_2_usg(timer_get_ticks());
}

uint32_t timer_get_ms(void) {
  return timer_get_us() / 1000;
}


/*  Esperas  */

void timer_sleep_us(uint32_t useg) {
  uint32_t ticks = usg_2_ticks(useg);
  uint32_t inicio = timer_get_ticks();

  /* La resta sin signo es correcta aunque el contador de ticks desborde */
  while ((timer_get_ticks() - inicio) < ticks);
}

void timer_sleep_ms(uint32_t mseg) {
  timer_sleep_us(mseg * 1000UL);
}


/*  Funciones de timeout y periódicas  */

/* Estado de cada canal del temporizador */
/* Función a ejecutar (0 -> canal libre) */
static void (*volatile funciones[TIMER_NUM_CANALES])(void); 
/* Periodo en ticks (0 -> es un timeout) */
static volatile uint32_t periodos[TIMER_NUM_CANALES]; 
/* Ticks que faltan tras el disparo programado */
static volatile uint32_t ticks_restantes[TIMER_NUM_CANALES]; 


/* Programa en el canal n el siguiente tramo de la espera, a partir del
   valor actual de TCn. Como TCn es de 16 bits, las esperas largas se
   dividen en tramos, todos de al menos margen_ticks.
   Se debe invocar con las interrupciones enmascaradas */
static void programa_tramo(uint8_t n) {
  uint32_t restantes = ticks_restantes[n];
  uint16_t tramo;

  if (restantes >= (0x10000UL + margen_ticks)) {
    /* Vuelta entera: TCn no cambia */
    tramo = 0;
    restantes -= 0x10000UL;
  } else if (restantes > 0xFFFFUL) {
    /* Con una vuelta entera quedaría un tramo final demasiado corto */
    tramo = 0x8000;
    restantes -= 0x8000UL;
  } else {
    /* Último tramo */
    tramo = restantes;
    restantes = 0;
  }
  TIMER_TC(n) += tramo;
  ticks_restantes[n] = restantes;
}

/* Deja libre el canal n. Con las interrupciones enmascaradas */
static void libera_canal(uint8_t n) {
  /* Enmascaramos su interrupción */
  _io_ports[M6812_TMSK1] &= (uint8_t)~TIMER_BIT(n);
  _io_ports[M6812_TFLG1] = TIMER_BIT(n);   /* Bajamos el banderín */
  funciones[n] = 0;
  periodos[n] = 0;
  ticks_restantes[n] = 0;
}

/* Tratamiento común de la interrupción del canal n */
static void atiende_canal(uint8_t n) {
  void (*funcion)(void) = funciones[n];

  /* Bajamos el banderín del canal */
  _io_ports[M6812_TFLG1] = TIMER_BIT(n);

  if (ticks_restantes[n]) {
    /* Aún no ha pasado todo el tiempo: siguiente tramo */
    programa_tramo(n);
    return;
  }

  if (periodos[n]) {
    /* Periódica: programamos el siguiente disparo respecto al actual
       (antes de ejecutar la función, para no depender de lo que tarde) */
    ticks_restantes[n] = periodos[n];
    programa_tramo(n);
  } else {
    /* Timeout: el canal queda libre antes de ejecutar la función, por si
       ésta quiere programar otro timeout */
    libera_canal(n);
  }

  funcion();
}

/* Rutinas de tratamiento de interrupción de cada canal */
void __attribute__((interrupt)) vi_ioc0(void) { 
  atiende_canal(0); 
}

#if TIMER_NUM_CANALES > 1
void __attribute__((interrupt)) vi_ioc1(void) { 
  atiende_canal(1); 
}
#endif

#if TIMER_NUM_CANALES > 2
void __attribute__((interrupt)) vi_ioc2(void) { 
  atiende_canal(2); 
}
#endif
#if TIMER_NUM_CANALES > 3
void __attribute__((interrupt)) vi_ioc3(void) {
  atiende_canal(3);
}
#endif

#if TIMER_NUM_CANALES > 4
void __attribute__((interrupt)) vi_ioc4(void) {
  atiende_canal(4);
}
#endif

#if TIMER_NUM_CANALES > 5
void __attribute__((interrupt)) vi_ioc5(void) {
  atiende_canal(5);
}
#endif

#if TIMER_NUM_CANALES > 6
void __attribute__((interrupt)) vi_ioc6(void) {
  atiende_canal(6);
}
#endif

#if TIMER_NUM_CANALES > 7
void __attribute__((interrupt)) vi_ioc7(void) {
  atiende_canal(7);
}
#endif

/* Busca un canal libre y programa en él la función indicada.
   si periodica no es 0 se repite cada useg; si no, se ejecuta una vez.
   Devuelve el canal utilizado o TIMER_ERROR */
static uint8_t programa_funcion(uint32_t useg, void (*ptFuncion)(void),
                                uint8_t periodica) {
  uint8_t n;
  uint32_t ticks;
  unsigned short mascara;

  if (!ptFuncion)
    return TIMER_ERROR;

  ticks = usg_2_ticks(useg);
  if (ticks < margen_ticks)
    ticks = margen_ticks;

  mascara = lock();

  for (n = 0; n < TIMER_NUM_CANALES; n++)
    if (!funciones[n])
      break;
  if (n == TIMER_NUM_CANALES) {
    restore(mascara);
    return TIMER_ERROR;
  }

  funciones[n] = ptFuncion;
  periodos[n] = periodica ? ticks : 0;
  ticks_restantes[n] = ticks;

  /* Desconectamos el canal de su pin para no afectarlo
     (canales 0 a 3 en TCTL2, canales 4 a 7 en TCTL1) */
  if (n < 4)
    _io_ports[M6812_TCTL2] &= (uint8_t)~(3 << (n << 1));
  else
    _io_ports[M6812_TCTL1] &= (uint8_t)~(3 << ((n - 4) << 1));
  /* Configuramos el canal como comparador de salida */
  _io_ports[M6812_TIOS] |= TIMER_BIT(n);

  /* El tiempo se cuenta desde el valor actual de TCNT */
  TIMER_TC(n) = _IO_PORTS_W(M6812_TCNT);
  programa_tramo(n);

  _io_ports[M6812_TFLG1] = TIMER_BIT(n);  /* Bajamos el banderín */
  _io_ports[M6812_TMSK1] |= TIMER_BIT(n); /* Habilitamos su interrupción */

  restore(mascara);
  return n;
}

/* Libera el canal id si está ocupado por una función del tipo indicado */
static void cancela_funcion(uint8_t id, uint8_t periodica) {
  unsigned short mascara;

  if (id >= TIMER_NUM_CANALES)
    return;

  mascara = lock();
  if (funciones[id] && ((periodos[id] != 0) == (periodica != 0)))
    libera_canal(id);
  restore(mascara);
}

uint8_t timer_set_timeout(uint32_t useg, void (*ptFuncion)(void)) {
  return programa_funcion(useg, ptFuncion, 0);
}

void timer_cancel_timeout(uint8_t id) {
  cancela_funcion(id, 0);
}

uint8_t timer_set_periodic(uint32_t useg, void (*ptFuncion)(void)) {
  return programa_funcion(useg, ptFuncion, 1);
}

void timer_stop_periodic(uint8_t id) {
  cancela_funcion(id, 1);
}


/*  Inicialización  */

void timer_init(uint8_t factor) {
  uint8_t n;

  /* Enmascaramos las interrupciones mientras se configura */
  lock();

  factor_tcm = factor & 0x07;
  margen_ticks = (TIMER_MARGEN_CICLOS >> factor_tcm) + 2;

  /* Establecemos el factor de división en TMSK2 y desactivamos
     el reset de TCNT con el canal 7, respetando el resto de bits */
  _io_ports[M6812_TMSK2] =
      (_io_ports[M6812_TMSK2] & ~(M6812B_TCRE | 0x07)) | factor_tcm;

  /* Los banderines se bajan escribiendo un 1, no al leer */
  _io_ports[M6812_TSCR] &= ~M6812B_TFFCA;

  /* Todos los canales de la librería libres y sin interrupción */
  _io_ports[M6812_TMSK1] &= (uint8_t)~TIMER_BITS_CANALES;
  _io_ports[M6812_TFLG1] = TIMER_BITS_CANALES;
  for (n = 0; n < TIMER_NUM_CANALES; n++) {
    funciones[n] = 0;
    periodos[n] = 0;
    ticks_restantes[n] = 0;
  }

  /* Arrancamos el temporizador */
  _io_ports[M6812_TSCR] |= M6812B_TEN;

  /* Origen de tiempos: 0 vueltas y el valor actual de TCNT. Si TCNT
     desborda justo entre bajar el banderín y leerlo, se repite para que
     esa vuelta no se cuente de más */
  numero_vueltas = 0;
  do {
    _io_ports[M6812_TFLG2] = M6812B_TOF;
    tcnt_inicio = _IO_PORTS_W(M6812_TCNT);
  } while (_io_ports[M6812_TFLG2] & M6812B_TOF);

  /* Habilitamos la interrupción de desbordamiento */
  _io_ports[M6812_TMSK2] |= M6812B_TOI;

  /* Habilitamos las interrupciones */
  unlock();
}