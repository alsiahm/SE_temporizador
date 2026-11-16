/* timer.c -- Librería de temporización para el 68HC12 (placa ADAPT912DT60)

   Autores: Alba Hidalgo Martín, Xuan Sun, Juhi Kamal Chatani Mansukhani

   ATENCIÓN: esta librería SOLO ES VÁLIDA SI EL RELOJ E VA A 8MHz
   (configuración de las placas Adapt). Las conversiones entre ticks y
   microsegundos están hechas para esa frecuencia.

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

#if (TIMER_NUM_CANALES < 1) || (TIMER_NUM_CANALES > 4)
# error "timer: TIMER_NUM_CANALES debe estar entre 1 y 4"
#endif

/* Registros y banderines utilizados
 *  TCNT:  contador del temporizador, se incrementa con cada tick
 *  TMSK2: factor de división (PR2:PR0), interrupción de desbordamiento (TOI)
 *  TFLG2: banderín de desbordamiento de TCNT (TOF)
 *  TCTL2: acción sobre el pin de los canales 0 a 3 (OMn, OLn)
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


/* ================= Conversión de unidades ================= */

/* Factor de división del temporizador (de 0 a 7) fijado en timer_init */
static uint8_t factor_tcm;

/* Ticks mínimos de un tramo de espera (TIMER_MARGEN_CICLOS en ticks) */
static uint16_t margen_ticks;

/* Pasa de microsegundos a ticks según el factor de división.
   Con reloj E a 8MHz hay 8 ticks por usg con factor 0, 1 tick por usg con
   factor 3 y 1/16 con factor 7. Si no cabe en 32 bits devuelve el máximo */
static uint32_t usg_2_ticks(uint32_t useg) {
  if (factor_tcm <= 3) {
    uint8_t desplaza = 3 - factor_tcm;
    if (useg > (0xFFFFFFFFUL >> desplaza))
      return 0xFFFFFFFFUL;
    return useg << desplaza;
  }
  return useg >> (factor_tcm - 3);
}

/* Pasa de ticks a microsegundos según el factor de división */
static uint32_t ticks_2_usg(uint32_t ticks) {
  if (factor_tcm <= 3)
    return ticks >> (3 - factor_tcm);
  return ticks << (factor_tcm - 3);
}


/* ================= Cuenta de tiempo ================= */

/* Vueltas completas que ha dado TCNT. Se modifica SOLO en vi_tov (y se
   pone a 0 en timer_init). Es volatile porque cambia en una interrupción */
static volatile uint16_t numero_vueltas;

/* Valor de TCNT en el momento de timer_init: origen de tiempos */
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
     lock/unlock) para poder invocarla con las interrupciones inhibidas */
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

  restore(mascara);

  /* 16 bits altos: vueltas, 16 bits bajos: TCNT. Se resta el origen */
  return (((uint32_t)vueltas << 16) | contador) - tcnt_inicio;
}

uint32_t timer_get_us(void) {
  return ticks_2_usg(timer_get_ticks());
}

uint32_t timer_get_ms(void) {
  return timer_get_us() / 1000;
}


/* ================= Esperas ================= */

void timer_sleep_us(uint32_t useg) {
  uint32_t ticks = usg_2_ticks(useg);
  uint32_t inicio = timer_get_ticks();

  /* La resta sin signo es correcta aunque el contador de ticks desborde */
  while ((timer_get_ticks() - inicio) < ticks);
}

void timer_sleep_ms(uint32_t mseg) {
  /* Se espera en tramos de 1 minuto para que los microsegundos (y los
     ticks) quepan en 32 bits sea cual sea el valor pedido */
  while (mseg > 60000UL) {
    timer_sleep_us(60000UL * 1000UL);
    mseg -= 60000UL;
  }
  timer_sleep_us(mseg * 1000UL);
}


/* ================= Funciones de timeout y periódicas ================= */

/* Estado de cada canal del temporizador */
typedef struct {
  void (*funcion)(void); /* Función a ejecutar. 0 ⇒ canal libre */
  uint32_t periodo;      /* Periodo en ticks. 0 ⇒ es un timeout */
  uint32_t restantes;    /* Ticks que faltan tras el disparo programado */
} timer_canal_t;

/* Se modifican en las interrupciones de los canales */
static volatile timer_canal_t canales[TIMER_NUM_CANALES];

/* Programa en el canal n el siguiente tramo de la espera, a partir del
   valor actual de TCn. Como TCn es de 16 bits, las esperas largas se
   dividen en tramos, todos de al menos margen_ticks.
   Se debe invocar con las interrupciones inhibidas */
static void programa_tramo(uint8_t n) {
  uint32_t restantes = canales[n].restantes;
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
  canales[n].restantes = restantes;
}

/* Deja libre el canal n. Con las interrupciones inhibidas */
static void libera_canal(uint8_t n) {
  /* Inhibimos su interrupción */
  _io_ports[M6812_TMSK1] &= (uint8_t)~TIMER_BIT(n);
  _io_ports[M6812_TFLG1] = TIMER_BIT(n);   /* Bajamos el banderín */
  canales[n].funcion = 0;
  canales[n].periodo = 0;
  canales[n].restantes = 0;
}

/* Tratamiento común de la interrupción del canal n */
static void atiende_canal(uint8_t n) {
  void (*funcion)(void) = canales[n].funcion;

  /* Bajamos el banderín del canal */
  _io_ports[M6812_TFLG1] = TIMER_BIT(n);

  if (!funcion) {
    /* No debería ocurrir: canal libre con la interrupción habilitada */
    _io_ports[M6812_TMSK1] &= (uint8_t)~TIMER_BIT(n);
    return;
  }

  if (canales[n].restantes) {
    /* Aún no ha pasado todo el tiempo: siguiente tramo */
    programa_tramo(n);
    return;
  }

  if (canales[n].periodo) {
    /* Periódica: programamos el siguiente disparo respecto al actual
       (antes de ejecutar la función, para no depender de lo que tarde) */
    canales[n].restantes = canales[n].periodo;
    programa_tramo(n);
  } else {
    /* Timeout: el canal queda libre antes de ejecutar la función, por si
       ésta quiere programar otro timeout */
    libera_canal(n);
  }

  funcion();
}

/* Rutinas de tratamiento de interrupción de cada canal */
void __attribute__((interrupt)) vi_ioc0(void) { atiende_canal(0); }
#if TIMER_NUM_CANALES > 1
void __attribute__((interrupt)) vi_ioc1(void) { atiende_canal(1); }
#endif
#if TIMER_NUM_CANALES > 2
void __attribute__((interrupt)) vi_ioc2(void) { atiende_canal(2); }
#endif
#if TIMER_NUM_CANALES > 3
void __attribute__((interrupt)) vi_ioc3(void) { atiende_canal(3); }
#endif

/* Busca un canal libre y programa en él la función indicada.
   periodica != 0 ⇒ se repite cada useg; si no, se ejecuta una vez.
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
    if (!canales[n].funcion)
      break;
  if (n == TIMER_NUM_CANALES) {
    restore(mascara);
    return TIMER_ERROR;
  }

  canales[n].funcion = ptFuncion;
  canales[n].periodo = periodica ? ticks : 0;
  canales[n].restantes = ticks;

  /* Desconectamos el canal de su pin para no afectarlo (OMn = OLn = 0) */
  _io_ports[M6812_TCTL2] &= (uint8_t)~(3 << (n << 1));
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
  if (canales[id].funcion && ((canales[id].periodo != 0) == (periodica != 0)))
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


/* ================= Inicialización ================= */

void timer_init(uint8_t factor) {
  uint8_t n;

  /* Inhibimos las interrupciones mientras se configura */
  lock();

  factor_tcm = factor & 0x07;
  margen_ticks = (TIMER_MARGEN_CICLOS >> factor_tcm) + 2;

  /* Establecemos el factor de división en TMSK2 (PR2:PR0) y desactivamos
     el reset de TCNT con el canal 7 (TCRE), respetando el resto de bits */
  _io_ports[M6812_TMSK2] =
      (_io_ports[M6812_TMSK2] & ~(M6812B_TCRE | 0x07)) | factor_tcm;

  /* Los banderines se bajan escribiendo un 1, no al leer */
  _io_ports[M6812_TSCR] &= ~M6812B_TFFCA;

  /* Todos los canales de la librería libres y sin interrupción */
  _io_ports[M6812_TMSK1] &= (uint8_t)~TIMER_BITS_CANALES;
  _io_ports[M6812_TFLG1] = TIMER_BITS_CANALES;
  for (n = 0; n < TIMER_NUM_CANALES; n++) {
    canales[n].funcion = 0;
    canales[n].periodo = 0;
    canales[n].restantes = 0;
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
