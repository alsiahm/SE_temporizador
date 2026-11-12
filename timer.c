
/**
 * LIBRERÍA EN C PARA EL USO DE TEMPORIZACIÓN EN EL MICROCONTROLADOR
 * Autores: Alba Hidalgo Martín, Xuan Sun, Juhi Kamal Chatani Mansukhani
 */
#include "timer.h"

/** EXPLICACIÓN DE PATILLAS/BANDERINES DEL TEMPORIZADOR
 * TCNT: Contador del temporizador, incrementa cada tick del temporizador
 * TOF: Bandera de vueltas temporizador, se activa cuando TCNT desborda
 * TOI: Interrupción de vueltas del temporizador, se activa cuando TOF
 * TC2: Registro de comparación del canal OC2, se configura con el valor de TC
 * OC2: Canal de salida temporizador, activa cuando TCNT coincide con valor TC2
 * TFLG2: Registro de banderas del temporizador, contiene la bandera TOF
 * TFLG1: Registro de banderas del temporizador, contiene la bandera C2
 * C2F: Bandera de coincidencia canal OC2, activa cuando TCNT coincide con TC2
 * C2I: Interrupción del canal OC2, se activa cuando C2F
 * No se usará el IC2 (canal de entrada) ni el OC1 (canal de salida 1)
 */


 // --Variables para el manejo del temporizador--
/*Factor de escalado de 0 a 7*/
uint8_t TCM_FACTOR;

uint32_t usg_2_ticks(uint32_t useg) {
  if (TCM_FACTOR <= 3)
  // Pasa de microsegundos a ticks
    return useg << (3 - TCM_FACTOR); 
  else 
  // Pasa de microsegundos a ticks
    return useg >> (TCM_FACTOR - 3); 
}
uint32_t msg_2_ticks(uint32_t mseg) {
  // Pasa de milisegundos a ticks
  return usg_2_ticks(mseg * 1000); 
}

uint32_t ticks_2_usg(uint32_t ticks) {
  if (TCM_FACTOR <= 3) 
  // Pasa de ticks a microsegundos
    return ticks >> (3 - TCM_FACTOR);
  else 
    return ticks << (TCM_FACTOR - 3); 
}

uint32_t ticks_2_msg(uint32_t ticks) {
   // Pasa de ticks a milisegundos
  return ticks_2_usg(ticks) / 1000;
}

// --Variables para el manejo de la función de usuario--
// Guarda el instante en el que se debe ejecutar
static uint32_t tiempo_inicio = 0;
// Puntero a la función que se ejecutará
void (*punteroFuncion)(void);

// --Variables globales para el contador absoluto del temporizador--
// Contador absoluto del temporizador; incrementa con cada tick.
static uint32_t contador_absoluto = 0;

// Volatile indica que esta variable puede cambiar fuera del flujo normal
// del programa, por ejemplo, desde una interrupción
// Contador de vueltas
volatile uint16_t numero_vueltas = 0;
// Contador de vueltas pendientes; incrementa con cada vuelta del temporizador
volatile uint16_t vueltas_pendientes = 0;
// --Variables para función periódica--
// Guarda el periodo en milisegundos para la función periódica
static uint32_t tiempo_periodico = 0;

// Función de vueltas del TCN
// Cada vez que TCNT pasa de 0xFFFF a 0x0000, se genera una interrupción de 
// desbordamiento
void __attribute__((interrupt)) vi_tov(void) {
  numero_vueltas++;
  // Limpiar la bandera TOF del registro TFLG2  (desbordamiento)
  _io_ports[M6812_TFLG2] = M6812B_TOF;
}

// Función de interrupción del canal OC2 (coincidencia con el valor del 
//registro TC2, el que se indique)
// Dedicada a ejecutar la función de usuario que se haya configurado con
// timer_set_timeout o timer_set_periodic
void __attribute__((interrupt)) vi_ioc2(void) {
  // Limpiar la bandera de interrupción del canal OC2
  _io_ports[M6812_TFLG1] = M6812B_C2F;
  // Guardar la función a ejecutar
  void (*funcionEjecutar)(void) = punteroFuncion; 
  // Si hay vueltas pendientes, no ejecutar la función aún
  if(vueltas_pendientes > 0){
    vueltas_pendientes--;
    return;
  }
  // Si es periódica, configurar el siguiente tiempo de ejecución
  if (tiempo_periodico > 0) { 
    tiempo_inicio += tiempo_periodico;
    // Configurar el tiempo de disparo. TC2 tiene 16 bits, por lo que se usan
    // los 16 bits menos significativos de tiempo_inicio.
    _IO_PORTS_W(M6812_TC2) += tiempo_periodico & 0xFFFF;    
    // Guardar el número de vueltas pendientes
    vueltas_pendientes = tiempo_periodico >> 16;
  } else {
    // Si es una función de timeout, desactivar la interrupción del canal OC2
    _io_ports[M6812_TMSK1] &= ~M6812B_C2I;
    // Indicar que ya no hay ninguna función pendiente
    punteroFuncion = 0;
  }
  if (funcionEjecutar) {
    // Ejecutar la función de usuario
    funcionEjecutar();
  }
}

// Función de inicialización del temporizador
void timer_init(void) {
  // Bloquear las interrupciones
  lock();

  // Configurar el factor de preescala del temporizador
	/*Factor de escalado de 0 a 7*/
	TCM_FACTOR = _io_ports[M6812_TMSK2] & 0x07;
  // Desactivar el reset del contador con OC7
  _io_ports[M6812_TMSK2] &= ~M6812B_TCRE;

  // Desactivar el borrado automático de las banderas
  _io_ports[M6812_TSCR] &= ~M6812B_TFFCA;

  // Inicializar el número de vueltas
  numero_vueltas = 0;

  // Limpiar la bandera de desbordamiento
  _io_ports[M6812_TFLG2] = M6812B_TOF;

  // Activar la interrupción de desbordamiento
  _io_ports[M6812_TMSK2] |= M6812B_TOI;

  // Activar el temporizador
  _io_ports[M6812_TSCR] |= M6812B_TEN;

    // Configurar el canal OC2 como salida
  _io_ports[M6812_TIOS] |= M6812B_IOS2;

  // Desactivar la interrupción del canal OC2
  _io_ports[M6812_TMSK1] &= ~M6812B_C2I;

  // Limpiar la bandera del canal OC2
  _io_ports[M6812_TFLG1] = M6812B_C2F;

  // Habilitar las interrupciones
  unlock();
}

// Función para obtener el valor del contador absoluto del temporizador
uint32_t timer_get_ticks(void) {
  // Bloquear las interrupciones
  lock();

  // Guardar el número de vueltas
  uint32_t vueltas = numero_vueltas;
  // Guardar el contador actual, por si hay una vuelta pendiente
  uint32_t contador = _IO_PORTS_W(M6812_TCNT);
  
  // Comprobar si hay una vuelta pendiente
  // Puede que TCNT haya dado una vuelta sin que se haya ejecutado la
  // interrupción.
  if (_io_ports[M6812_TFLG2] & M6812B_TOF) {
    vueltas++;
    // Limpiar la bandera para que no se vuelva a contar
    _io_ports[M6812_TFLG2] = M6812B_TOF;
    // Actualizar el contador absoluto con el valor actual de TCNT
    contador = _IO_PORTS_W(M6812_TCNT);
    // Actualizar el número de vueltas
    numero_vueltas = vueltas;
  }
  // Calcular el contador absoluto: 16 bits superiores para las vueltas y
  // 16 bits inferiores para el valor del contador.
  contador_absoluto = (vueltas << 16) | contador;
  // Volver a habilitar las interrupciones
  unlock();
  // Devolver el valor del contador absoluto
  return contador_absoluto;
}

// Función para obtener el tiempo transcurrido en microsegundos desde 
// que se inició el temporizador
uint32_t timer_get_us(void) {
  return ticks_2_usg(timer_get_ticks());
}

uint32_t timer_get_ms(void) {
  return ticks_2_msg(timer_get_ticks());
}

void timer_sleep_ms(uint32_t milisegundos) {
  // Guardar el tiempo inicial
  uint32_t inicio = timer_get_ms();

  // Esperar hasta que pase el tiempo indicado
  while (timer_get_ms() - inicio < milisegundos);
}

void timer_sleep_us(uint32_t u_seg) {
	timer_sleep_ms(u_seg / 1000);
}

// Función para configurar un temporizador que ejecute una función después 
// de un tiempo determinado
// Se ejecutará una sola vez, y se desactivará automáticamente después de 
// ejecutarse
void timer_set_timeout(uint32_t u_seg, void (*ptFuncion)(void)) {
  //time_out
  uint32_t time_out = usg_2_ticks(u_seg);
  // Calcular cuándo se debe ejecutar  
  tiempo_inicio = timer_get_ticks() + time_out;
  serial_print("\r\nantes del lock");
  // Bloquear las interrupciones
  lock();
  // Guardar la función que queremos ejecutar
  punteroFuncion = ptFuncion;
  // Indicar que es una función de timeout, no periódica
  tiempo_periodico = 0;
  // Guardar el número de vueltas pendientes
  vueltas_pendientes = time_out >> 16;
  // Configurar el tiempo de disparo. TC2 tiene 16 bits, así que se usan los
  // 16 bits menos significativos de tiempo_inicio.
  _IO_PORTS_W(M6812_TC2) = tiempo_inicio & 0xFFFF;
  // Volver a habilitar las interrupciones
  // Habilitar la interrupción del canal OC2
  _io_ports[M6812_TMSK1] |= M6812B_C2I;
  // Limpiar la bandera de interrupción del canal OC2
  _io_ports[M6812_TFLG1] = M6812B_C2F;
  unlock();
}

void timer_set_periodic(uint32_t u_seg, void (*ptFuncion)(void)) {
  // Guardar el periodo en milisegundos
  tiempo_periodico = usg_2_ticks(u_seg);
  // Guardar el tiempo de inicio
  tiempo_inicio = timer_get_ticks() + tiempo_periodico;
  // Bloquear las interrupciones
  lock();
  // Guardar la función que queremos ejecutar
  punteroFuncion = ptFuncion;
  // Guardar el número de vueltas pendientes
  vueltas_pendientes = tiempo_periodico >> 16;
  _IO_PORTS_W(M6812_TC2) = _IO_PORTS_W(M6812_TCNT) + tiempo_periodico; 
  // Configurar el temporizador para que genere interrupciones periódicas
  // Limpiar la bandera de interrupción del canal OC2
  _io_ports[M6812_TFLG1] = M6812B_C2F;
  // Habilitar la interrupción del canal OC2
  _io_ports[M6812_TMSK1] |= M6812B_C2I;
  
  // Configurar el tiempo de disparo
  unlock();
}


