/* timer.h -- Librería de temporización para el 68HC12 (placa ADAPT912DT60)

   Autores: Alba Hidalgo Martín, Xuan Sun, Juhi Kamal Chatani Mansukhani
*/

#ifndef TIMER_H
#define TIMER_H

#include <types.h>

/*! @defgroup timer Temporización

  Conjunto de funciones para medir el tiempo transcurrido, realizar esperas
  y programar la ejecución diferida o periódica de funciones utilizando el
  módulo temporizador del 68HC12.

  @attention La librería **solo es válida si el reloj E va a 8MHz**
  (configuración de las placas Adapt). Si `M6812_CPU_E_CLOCK` tiene otro
  valor la compilación se detiene con un error.

  ## Recursos que utiliza

  - El contador `TCNT` y su interrupción de desbordamiento (`vi_tov`).
  - Los canales 0 a 3 del temporizador (OC0 a OC3) como comparadores de
    salida, con sus interrupciones (`vi_ioc0` a `vi_ioc3`). No se actúa
    sobre los pines asociados a los canales.
  - Los canales 4 a 7 no se utilizan y quedan libres para otros usos.

  Cada función de timeout o periódica activa ocupa un canal, por lo que
  se pueden tener simultáneamente hasta `TIMER_NUM_CANALES` (4 por defecto)
  entre ambos tipos, en cualquier combinación.

  ## Resolución y alcance

  Dependen del factor de división pasado a timer_init():

  | Factor | Divisor | Resolución | Ticks por usg | Alcance      |
  |:------:|:-------:|-----------:|:-------------:|-------------:|
  |   0    |    1    |  125 nsg   |      8        |   536 sg     |
  |   1    |    2    |  250 nsg   |      4        |  1073 sg     |
  |   2    |    4    |  500 nsg   |      2        |  2147 sg     |
  |   3    |    8    |    1 usg   |      1        |  4294 sg     |
  |   4    |   16    |    2 usg   |     1/2       |  4294 sg     |
  |   5    |   32    |    4 usg   |     1/4       |  4294 sg     |
  |   6    |   64    |    8 usg   |     1/8       |  4294 sg     |
  |   7    |  128    |   16 usg   |     1/16      |  4294 sg     |

  El alcance es el tiempo máximo que se puede medir o programar: pasado ese
  tiempo timer_get_ticks(), timer_get_us() y timer_get_ms() vuelven a
  empezar desde 0. Los retrasos mayores que el alcance se recortan a éste
  (salvo en timer_sleep_ms(), que no tiene esa limitación).

  El retraso mínimo de las funciones de timeout y periódicas es de unos
  64 usg (512 ciclos del reloj E): los valores menores se aumentan
  hasta ese mínimo.

  ## Ejemplo

  ~~~{.c}
  void parpadea(void) {
    _io_ports[M6812_PORTG] ^= M6812B_PG7;
  }

  int main(void) {
    timer_init(3);                                  // resolución de 1 usg
    uint8_t id = timer_set_periodic(500000UL, parpadea);
    timer_sleep_ms(10000);
    timer_stop_periodic(id);
    while (1);
  }
  ~~~

  @{
*/

/** Número de canales del temporizador que utiliza la librería.

    Es también el máximo de funciones de timeout y periódicas que puede
    haber a la vez. Por defecto se usan los canales del 0 al 3. Se puede
    definir con un valor menor (de 1 a 3) al compilar `timer.c` para usar
    solo los canales del `0` al `TIMER_NUM_CANALES - 1`.
*/
#ifndef TIMER_NUM_CANALES
# define TIMER_NUM_CANALES 4
#endif

/** Valor devuelto por timer_set_timeout() y timer_set_periodic() cuando
    no se ha podido programar la función. */
#define TIMER_ERROR 0xFF

/*! Inicializa el temporizador.

    Establece en `TMSK2` el factor de división de la frecuencia del
    temporizador, pone a 0 la cuenta de tiempo, cancela todas las funciones
    de timeout y periódicas que pudiera haber y arranca el temporizador.

    Debe invocarse antes que cualquier otra función de la librería.
    Al terminar deja las interrupciones habilitadas, ya que la librería
    las necesita para funcionar.

    @param factor factor de división de la frecuencia del temporizador,
    de 0 a 7. La frecuencia del reloj E se divide por `2^factor` (ver
    tabla en la descripción del grupo). Solo se usan los 3 bits bajos.

    @see timer_get_ticks
*/
void timer_init(uint8_t factor);

/*! Devuelve los ticks del temporizador transcurridos desde timer_init().

    La duración de cada tick depende del factor de división indicado
    en timer_init(). Se puede invocar con las interrupciones inhibidas
    y desde las funciones de timeout y periódicas.

    @return número de ticks transcurridos (32 bits).

    @see timer_get_us, timer_get_ms
*/
uint32_t timer_get_ticks(void);

/*! Devuelve los microsegundos transcurridos desde timer_init().

    @return tiempo transcurrido en microsegundos.

    @see timer_get_ticks, timer_get_ms
*/
uint32_t timer_get_us(void);

/*! Devuelve los milisegundos transcurridos desde timer_init().

    @return tiempo transcurrido en milisegundos.

    @see timer_get_ticks, timer_get_us
*/
uint32_t timer_get_ms(void);

/*! Espera activa del número de microsegundos indicado.

    No utiliza ningún canal del temporizador. Necesita que las
    interrupciones estén habilitadas si la espera es mayor que una vuelta
    del contador `TCNT`.

    @param useg microsegundos a esperar.

    @see timer_sleep_ms
*/
void timer_sleep_us(uint32_t useg);

/*! Espera activa del número de milisegundos indicado.

    Se apoya en timer_sleep_us() para no perder resolución.

    @param mseg milisegundos a esperar.

    @see timer_sleep_us
*/
void timer_sleep_ms(uint32_t mseg);

/*! Programa la ejecución de una función, una sola vez, pasado un tiempo.

    La función se ejecuta dentro de una rutina de tratamiento de
    interrupción, por lo que debe ser breve. Una vez ejecutada, el canal
    queda libre y el identificador deja de ser válido. Desde la propia
    función se puede programar un nuevo timeout.

    Se pueden tener varios timeouts y funciones periódicas simultáneamente
    (hasta `TIMER_NUM_CANALES` en total).

    @param useg microsegundos que deben pasar hasta la ejecución.
    @param ptFuncion función a ejecutar.

    @return identificador del timeout (necesario para cancelarlo) o
    `TIMER_ERROR` si no quedan canales libres o `ptFuncion` es nulo.

    @see timer_cancel_timeout, timer_set_periodic
*/
uint8_t timer_set_timeout(uint32_t useg, void (*ptFuncion)(void));

/*! Cancela un timeout pendiente de ejecutarse.

    No hace nada si el identificador no corresponde a un timeout pendiente.

    @param id identificador devuelto por timer_set_timeout().

    @see timer_set_timeout
*/
void timer_cancel_timeout(uint8_t id);

/*! Programa la ejecución periódica de una función.

    La primera ejecución se produce pasado un periodo. Los disparos se
    programan respecto al disparo anterior, por lo que el periodo no
    acumula error. La función se ejecuta dentro de una rutina de
    tratamiento de interrupción, por lo que debe ser breve (y, en cualquier
    caso, durar menos que el periodo).

    Se pueden tener varias funciones periódicas y timeouts simultáneamente
    (hasta `TIMER_NUM_CANALES` en total).

    @param useg periodo en microsegundos.
    @param ptFuncion función a ejecutar.

    @return identificador de la función periódica (necesario para
    detenerla) o `TIMER_ERROR` si no quedan canales libres o `ptFuncion`
    es nulo.

    @see timer_stop_periodic, timer_set_timeout
*/
uint8_t timer_set_periodic(uint32_t useg, void (*ptFuncion)(void));

/*! Detiene una función periódica.

    No hace nada si el identificador no corresponde a una función
    periódica activa.

    @param id identificador devuelto por timer_set_periodic().

    @see timer_set_periodic
*/
void timer_stop_periodic(uint8_t id);

/**@}*/

#endif /* TIMER_H */
