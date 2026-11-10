# Click Frames GD

## v0.4.0 / 2026-11-10 / 00:26 UTC / GD 2.2081 / Geode 5.11.2

-Correcciones:
- La verificación reintenta con desfases 0, +1, -1, +2 y -2 ticks antes de fallar; los desvíos de posición se registran como advertencia.
- Si cambia el desfase de la run, se descartan los márgenes anteriores y se vuelven a medir. Se eliminan las liberaciones posteriores al final que no llegaron a reproducirse.
- Al pausar, detener o salir del análisis se restauran la velocidad previa y la visibilidad del nivel.
- Los paquetes rechazan rutas inválidas, nombres duplicados, datos sobrantes y tamaños excesivos. Si falla el reemplazo de una run se intenta restaurar su copia anterior.

-Mejoras:
- Verify / Analyze muestran una pantalla de progreso y ocultan los objetos durante las pruebas.
- Velocidad de análisis configurable entre 1 y 3 (por defecto 1.5).
- Los fallos de verificación muestran un aviso al volver a pausa y detalles en el log con prefijo `[CF]`.

-Cambios:
- Salto directo de v0.2.2 a v0.4.0; se incorporan también las funciones del parche intermedio sin publicar v0.3.0.
- Verify comprueba que se termina el nivel y se reproducen las pulsaciones, sin exigir la misma trayectoria o tick final exacto.
- Las grabaciones se organizan en `levels/<nombre>-<id>/`; los archivos v0.2.x se migran al abrir su nivel.

-Implementaciones:
- EXPORT reúne las runs del nivel en `exports/<nivel>.cfpack`. IMPORT carga los paquetes colocados en `imports/` y deja los procesados con extensión `.done`.
- Milisegundos entre pulsaciones en Play y ancho de cada ventana en milisegundos sobre los círculos.
- Pruebas automáticas del formato de paquetes y del desplazamiento completo de inputs, incluidos límites y datos inválidos.

## Compartir grabaciones

En pausa → Frames → EXPORT se muestra la ruta del `.cfpack`. Copia un paquete recibido a la carpeta `imports` del mod y pulsa IMPORT. El paquete conserva las runs y los márgenes; no contiene sonidos ni código. El nivel debe coincidir en nombre, ID y contenido. Puedes reproducir un análisis importado con Play; si no reproduce correctamente, detén la reproducción y vuelve a grabar o analizar.

La velocidad de análisis afecta al tiempo del juego, no cambia la unidad de medida: 240 ticks por segundo, aproximadamente 4.17 ms por tick. Al pausar se restaura la velocidad previa; al continuar se aplica de nuevo la velocidad de análisis. No se ha probado todavía en el dispositivo Android del usuario.

## v0.2.2 / 2026-11-09 / 22:48 UTC / GD 2.2081 / Geode 5.11.2

-Correcciones:
- Los controles del mod conservan su tamaño al pulsarlos; el tamaño visual se aplica al sprite, no al control táctil.
- Se elimina el cambio de prioridad táctil de los menús de práctica en cada fotograma.
- Analyze valida la compatibilidad antes de cerrar el menú.

-Mejoras:
- Menú con tarjetas, instrucciones, estado de la sesión y resumen de ventanas por categoría.
- Los botones grises indican funciones que requieren completar el paso anterior.

-Cambios:
- Flujo visible: Record → Analyze → Play. Play también sirve para repetir el showcase.
- Stop / Save conserva el análisis parcial y actualiza el menú.

-Implementaciones:
- Ayuda integrada y compatibilidad visible en el menú.
- Este README usa versión / fecha / hora UTC / compatibilidad y las cuatro categorías en cada actualización.

## Cómo probarlo

1. Abre un nivel clásico, pausa y toca **Frames**, abajo en el centro.
2. **Record** inicia una grabación nueva desde cero en práctica. Completa el nivel; puedes morir y usar checkpoints. Al volver a un checkpoint se descarta el tramo fallido. Una nueva grabación reemplaza la anterior de ese nivel.
3. Al terminar se guarda la run, se reinicia la escena y se abre la pausa. Entra en **Frames → Analyze**.
4. El mod primero reproduce la run original para verificarla. Después repite el nivel muchas veces adelantando y retrasando cada pulsación y liberación para medir su margen. Puede tardar bastante: no es un escaneo instantáneo de los objetos del nivel.
5. Cuando finalice, abre **Frames → Play**. Verás círculos en las posiciones grabadas, márgenes, sonidos y el contador. Las categorías vacías se ocultan en el showcase.

| Botón | Función |
| --- | --- |
| Record | Nueva grabación en práctica desde cero. |
| Analyze | Verifica la run completa y calcula márgenes; retoma eventos pendientes de un análisis interrumpido. |
| Play | Reproduce el showcase ya analizado; sirve también como Replay. |
| Stop / Save | Detiene la tarea activa y guarda el progreso parcial; una grabación incompleta no se puede analizar. |
| Settings | Sonidos, círculos, escalas y radio de búsqueda. |
| Help | Explica el flujo dentro del juego. |
| X | Cierra el menú y vuelve a la pausa. |

Para detener un análisis, pausa y abre Frames → Stop / Save. Para continuarlo usa Analyze.
Usa física de 240 TPS, sin Click Between Frames ni ignore damage. Plataforma no está soportado.
La compatibilidad declarada es GD 2.2081 con Geode 5.11.2 en Windows, Android32 y Android64.

El margen es el intervalo continuo de ticks válidos alrededor del click original, manteniendo los demás inputs fijos. Una única posición válida (desplazamiento de cero frames) pertenece a 1F / frame perfect. El signo `+` indica que la ventana alcanza el límite de búsqueda y podría ser mayor. El análisis depende de completar la reproducción de la run. En v0.4.0 un desvío de posición se registra como advertencia; una muerte, un bloqueo o un paso físico incompatible detienen la prueba.

## Prueba táctil en Android

Abre y cierra Frames varias veces, pulsa Settings y Help, vuelve con X y comprueba que los botones mantienen su tamaño. Después inicia Record y prueba colocar y quitar checkpoints. Finalmente completa un nivel corto, analiza y reproduce. La compilación automática no sustituye esta prueba en el dispositivo con los demás mods instalados.

## v0.2.1 / 2026-11-09 / 22:26 UTC / GD 2.2081 / Geode 5.11.2

-Correcciones:
- Reinicio y pausa diferidos al terminar Record, Analyze o Play para evitar quedar bloqueado al 100%.

-Mejoras:
- Retorno a la pausa después de completar o interrumpir una reproducción automática.

-Cambios:
- La finalización guarda la sesión antes de preparar la escena.

-Implementaciones:
- Protección contra solicitudes duplicadas de retorno a pausa.
