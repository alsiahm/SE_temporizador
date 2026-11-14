
/**
 * LIBRERÍA EN C PARA EL USO DE TEMPORIZACIÓN EN EL MICROCONTROLADOR
 * Autores: Alba Hidalgo Martín, Xuan Sun, Juhi Kamal Chatani Mansukhani
 */
#include "timer.h"

/** EXPLICACIÓN DE PATILLAS/BANDERINES DEL TEMPORIZADOR
 * TCNT: Contador del temporizador, incrementa cada tick del temporizador
 * TOF: Bandera de vueltas temporizador, se activa cuando TCNT desborda
 * TOI: Interrupción de vueltas del temporizador, se activa cuando TOF
 * TC2: Registro de comparación del canal OC2, se configura con el valor de
 *      TC
 * OC2: Canal de salida temporizador, activa cuando TCNT coincide con valor
 *      TC2
 * TFLG2: Registro de banderas del temporizador, contiene la bandera TOF
 * TFLG1: Registro de banderas del temporizador, contiene la bandera C2
 * C2F: Bandera de coincidencia canal OC2, activa cuando TCNT coincide con 
 *      TC2
 * C2I: Interrupción del canal OC2, se activa cuando C2F
 * No se usará el IC2 (canal de entrada) ni el OC1 (canal de salida 1)
 */

/*Factor de escalado de 0 a 7*/
uint8_t TCM_FACTOR;

/**
 * @brief Función para pasar de microsegundos a ticks en base al factor
 * @param useg
 * @return uint32_t
 */
uint32_t usg_2_ticks(uint32_t useg) {
  if (TCM_FACTOR <= 3)
  // Pasa de microsegundos a ticks
    return useg << (3 - TCM_FACTOR); 
  else 
  // Pasa de microsegundos a ticks
    return useg >> (TCM_FACTOR - 3); 
}

/**
 * @brief Función para pasar de milisegundos a ticks
 * @param mseg
 * @return uint32_t
 */
uint32_t msg_2_ticks(uint32_t mseg) {
  // Pasa de milisegundos a ticks
  return usg_2_ticks(mseg * 1000); 
}

/**
 * @brief Función para pasar de ticks a microsegundos
 * @param ticks
 * @return uint32_t
 */
uint32_t ticks_2_usg(uint32_t ticks) {
  if (TCM_FACTOR <= 3) 
    // Pasa de ticks a microsegundos
    return ticks >> (3 - TCM_FACTOR);
  else 
    return ticks << (TCM_FACTOR - 3); 
}

/**
 * @brief Función para pasar de ticks a milisegundos
 * @param ticks
 * @return uint32_t
 */
uint32_t ticks_2_msg(uint32_t ticks) {
  // Pasa de ticks a milisegundos
  return ticks_2_usg(ticks) / 1000;
}

// Variables para el manejo de las distintas funciones
static const uint16_t registro_TC[8] = {M6812_TC0, M6812_TC1, M6812_TC2, M6812_TC3, M6812_TC4, M6812_TC5, M6812_TC6, M6812_TC7};
static const uint8_t bandera_CnF[8] = {M6812B_C0F, M6812B_C1F, M6812B_C2F, M6812B_C3F, M6812B_C4F, M6812B_C5F, M6812B_C6F, M6812B_C7F};
static const uint8_t interrupcion_CnI[8] = {M6812B_C0I, M6812B_C1I, M6812B_C2I, M6812B_C3I, M6812B_C4I, M6812B_C5I, M6812B_C6I, M6812B_C7I};
static const uint8_t canal_OCn[8] = {M6812B_IOS0, M6812B_IOS1, M6812B_IOS2, M6812B_IOS3, M6812B_IOS4, M6812B_IOS5, M6812B_IOS6, M6812B_IOS7};

static uint32_t periodos[8]; // Si 0, timeout
static uint16_t vueltas_pendientes[8]; // Número de vueltas pendientes para cada canal
static void (*funciones[8])(void); // Punteros a las funciones de usuario para cada canal
// Si el valor es 0, el canal está libre


// Variables globales para el contador absoluto del temporizador
// Contador absoluto del temporizador que incrementa con cada tick
static uint32_t contador_absoluto = 0;

// Volatile indica que esta variable puede cambiar fuera del flujo normal
// del programa, por ejemplo, desde una interrupción
// Contador de vueltas
volatile uint16_t numero_vueltas = 0;


/**
 * @brief Función activada por interrupción de desbordamiento que actualiza
 *        el número de desbordamientos
 * @param
 */
void __attribute__((interrupt)) vi_tov(void) {
  numero_vueltas++;
  // Limpiar la bandera TOF del registro TFLG2  (desbordamiento)
  _io_ports[M6812_TFLG2] = M6812B_TOF;
}

/**
 * @brief Función de búsqueda de canal libre (como cola)
 * Busca el primer canal OC libre (sin función asignada) y devuelve su índice
 * Debe llamarse con las interrupciones bloqueadas
 * @return int8_t: Devuelve el índice del canal libre (0-7) o -1
 */
int8_t buscar_canal_libre(void) {
  for (uint8_t i = 0; i < 8; i++) {
    if (funciones[i] == NULL) {
      return i; // Canal libre encontrado
    }
  }
  return -1; // No hay canales libres
}

/**
 * @brief Función para programar el canal del timeout, 
 * periódico e interruptor de la función de usuario
 * Programa el prox disparo del canal
 * @param canal: Canal a programar (0-7)
 * @param base : Base de tiempo en ticks de TCn
 * @param ticks : Número de ticks hasta el próximo disparo
 */
void programar_canal(uint8_t canal, uint32_t base, uint32_t ticks) {
  uint16_t tcn = ticks & 0xFFFF; // Valor de TCn (16 bits)
  uint16_t vueltas = ticks >> 16; // Número de vueltas pendientes (si hay

  if (tcn == 0 && vueltas > 0) {
    vueltas--; // Si tcn es 0, se necesita una vuelta menos
  }

  vueltas_pendientes[canal] = vueltas; // Guardar el número de vueltas pendientes
  _IO_PORTS_W(registro_TC[canal]) = (base + tcn); // Configurar el tiempo de disparo
}

/**
 * @brief Función para liberal el canal para otro timer
 * @param canal: Canal a liberar (0-7)
 */
void liberar_canal(uint8_t canal) {
  // Deshabilitar la interrupción del canal OCn
  _io_ports[M6812_TMSK1] &= ~interrupcion_CnI[canal];

  // Limpiar la bandera de interrupción del canal OCn
  _io_ports[M6812_TFLG1] = bandera_CnF[canal];

  _io_ports[M6812_TIOS] &= ~canal_OCn[canal]; // Configurar el canal como entrada (libre)
  // Indicar que el canal está libre
  funciones[canal] = NULL;
  periodos[canal] = 0;
  vueltas_pendientes[canal] = 0;
}

/**
 * @brief Func. llamada por interrupciones que atiende a la interrupción del canal OC2 y ejecuta la función de usuario
 * @param canal: Canal que ha generado la interrupción (0-7)
 */
void atender_canal(uint8_t canal) {
  // Limpiar la bandera de interrupción del canal OCn
  _io_ports[M6812_TFLG1] = bandera_CnF[canal];

  // Guardar la función a ejecutar
  void (*funcionEjecutar)(void) = funciones[canal];

  if (funcionEjecutar == NULL) {
    // No hay función asignada a este canal, salir
    return;
  }

  // Si hay vueltas pendientes, no ejecutar la función aún
  if (vueltas_pendientes[canal] > 0) {
    vueltas_pendientes[canal]--;
    return;
  }

  // Si es periódica, configurar el siguiente tiempo de ejecución
  if (periodos[canal] > 0) {
    programar_canal(canal, _IO_PORTS_W(registro_TC[canal]), periodos[canal]);
  } else {
    // Si es una función de timeout, desactivar la interrupción del canal OCn
    liberar_canal(canal);
  }

  funcionEjecutar(); // Ejecutar la función de usuario
}

// Interrupciones de todos los canales OCn (0-7)
void __attribute__((interrupt)) vi_ioc0(void) {
  atender_canal(0); // Canal OC0
}
void __attribute__((interrupt)) vi_ioc1(void) {
  atender_canal(1); // Canal OC1
}
void __attribute__((interrupt)) vi_ioc2(void) {
  atender_canal(2); // Canal OC2
}
void __attribute__((interrupt)) vi_ioc3(void) {
  atender_canal(3); // Canal OC3
}
void __attribute__((interrupt)) vi_ioc4(void) {
  atender_canal(4); // Canal OC4
}
void __attribute__((interrupt)) vi_ioc5(void) {
  atender_canal(5); // Canal OC5
}
void __attribute__((interrupt)) vi_ioc6(void) {
  atender_canal(6); // Canal OC6
}
void __attribute__((interrupt)) vi_ioc7(void) {
  atender_canal(7); // Canal OC7
}



/**
 * @brief Función para iniciar el temporizador
 * @param
 */
void timer_init(void) {
  // Bloquear las interrupciones
  lock();

  // Configurar el factor de preescala del temporizador
	// Factor de escalado de 0 a 7
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

  // Configuración de los 8 canales
  _io_ports[M6812_TMSK1] = 0;
  _io_ports[M6812_TFLG1] = 0xFF; // Limpiar todas las banderas de los canales
  _io_ports[M6812_TCTL1] = 0; // el disparo no modifica ningún pin
  _io_ports[M6812_TCTL2] = 0; // Configurar todos los canales como entrada (libres)
  _io_ports[M6812_OC7M] = 0; // Configurar todos los canales como entrada (libres)
  
  for (uint8_t i = 0; i < 8; i++) {
    funciones[i] = NULL; // Inicializar todos los canales como libres
    periodos[i] = 0; // Inicializar todos los periodos a 0
    vueltas_pendientes[i] = 0; // Inicializar todas las vueltas pendientes a 0
  }

  // Habilitar las interrupciones
  unlock();
}


/**
 * @brief Función para obtener el valor del contador absoluto del 
 *        temporizador
 * @param
 * @return
 */
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


/**
 * @brief Función para obtener el tiempo transcurrido en microsegundos 
 *        desde que se inició el temporizador
 * @return uint32_t: Devuelve el tiempo en microsegundos
 */
uint32_t timer_get_us(void) {
  return ticks_2_usg(timer_get_ticks());
}


/**
 * @brief Función para obtener el tiempo transcurrido en milisegundos
 *        desde que se inició el temporizador
 * @return uint32_t: Devuelve el tiempo en milisegundos
 */
uint32_t timer_get_ms(void) {
  return ticks_2_msg(timer_get_ticks());
}


/**
 * @brief Función que espera una cantidad de tiempo indicada sin hacer nada
 * @param milisegundos a esperar
 */
void timer_sleep_ms(uint32_t milisegundos) {
  // Guardar el tiempo inicial
  uint32_t inicio = timer_get_ms();

  // Esperar hasta que pase el tiempo indicado
  while (timer_get_ms() - inicio < milisegundos);
}


/**
 * @brief 
 * @param
 */
void timer_sleep_us(uint32_t u_seg) {
	timer_sleep_ms(u_seg / 1000);
}

/**
 * @brief Func que asigna un canal libre a una función y lo programa
 * @param ticks: Número de ticks hasta el próximo disparo
 * @param periodo: Número de ticks entre disparos (0 si es timeout)
 * @param ptFuncion: Puntero a la función de usuario
 * @return int8_t: Devuelve el canal asignado (0-7) o -1 si no hay canales libres
 */
int8_t iniciar_canal(uint32_t ticks, uint32_t periodo, void (*ptFuncion)(void)) {
  if (ptFuncion == NULL) {
    return -1; // No se puede asignar una función nula
  }
  lock(); // Bloquear las interrupciones

  int8_t canal = buscar_canal_libre(); // Buscar un canal libre
  if (canal >= 0) {
    funciones[canal] = ptFuncion; // Asignar la función al canal
    periodos[canal] = periodo; // Asignar el periodo al canal

    _io_ports[M6812_TIOS] |= canal_OCn[canal]; // Configurar el canal como salida (ocupado)
    programar_canal(canal, _IO_PORTS_W(M6812_TCNT), ticks); // Programar el canal con el tiempo de disparo
    _io_ports[M6812_TMSK1] |= interrupcion_CnI[canal]; // Habilitar
    _io_ports[M6812_TFLG1] = bandera_CnF[canal]; // Limpiar la bandera de interrupción del canal OCn
  }

  unlock();
  return canal;
}

/**
 * @brief Función para configurar un temporizador que ejecute una función 
 *        después de un tiempo determinado
 *        Se ejecutará una sola vez, y se desactivará automáticamente 
 *        después de ejecutarse
 * @param u_seg
 * @param ptFuncion
 */
int8_t timer_set_timeout(uint32_t u_seg, void (*ptFuncion)(void)) {
  uint32_t ticks = usg_2_ticks(u_seg); // Convertir microsegundos a ticks
  return iniciar_canal(ticks, 0, ptFuncion); // Iniciar un canal con periodo 0 (timeout)
}


/**
 * @brief Función para configurar un temporizador que ejecute una función 
 *        periódicamente
 * @param u_seg
 * @param ptFuncion
 */
int8_t timer_set_periodic(uint32_t u_seg, void (*ptFuncion)(void)) {
  uint32_t ticks = usg_2_ticks(u_seg); // Convertir microsegundos a ticks
  return iniciar_canal(ticks, ticks, ptFuncion); // Iniciar un canal con periodo igual al tiempo de disparo (periódico)
}


/**
 * @brief Función para detener la ejecución  configurada con 
 *         Si no hay ninguna función periódica activa 
 *        no hace nada (así no se cancela un posible timeout pendiente)
 */
void timer_stop(uint8_t canal) {
  if (canal > 7) {
    return; // Canal inválido
  }
  lock(); // Bloquear las interrupciones
  if (funciones[canal] != 0) {
    liberar_canal(canal); // Liberar el canal
  }

  // Volver a habilitar las interrupciones
  unlock(); 
}