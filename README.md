# Click Frames GD

## v0.7.0 / 2026-11-10 / 02:07 UTC / GD 2.2081 / Geode 5.11.2

-Correcciones:
- Las liberaciones de cubo, ball, UFO y spider ya no se analizan ni generan círculos, sonidos o conteo. Todos los inputs siguen grabándose y reproduciéndose.
- Ship, wave, robot y swing conservan la medición de liberaciones; el modo de cada input se guarda en nuevas capturas.

-Mejoras:
- Círculos huecos con etiqueta única, por ejemplo `6 F` o `25+ F`. Se retira el desglose temprano/tardío y los ms del círculo, manteniéndolos en los archivos.
- Showcase sin mensaje superior; conserva filas y círculos.

-Cambios:
- Milisegundos entre pulsaciones del showcase abajo a la izquierda. Se actualizan en cada pulsación desde la segunda, con un destello de color durante 60 ticks. No mide latencia de red ni retraso del dispositivo.
- Los ms usan tiempo del juego: diferencia de ticks / 240. El speedhack cambia la duración real, no esta unidad.
- Capturas nuevas en CFR 3 con modo por input. Se leen las antiguas CFR 2; en ellas se cuentan solo pulsaciones porque se desconoce el modo. Los lectores antiguos pueden no admitir CFR 3. Los backups conservan la captura anterior al reemplazo.

-Implementaciones:
- Settings: Milliseconds horizontal/vertical position (%) para mover el indicador por pantalla, con límites ajustados a su tamaño.
- Filtro compartido de eventos para el análisis, estado Ready, filas, sonidos, marcadores y resumen.

`6F -5/+0` significaba seis ticks válidos: -5, -4, -3, -2, -1 y 0. A 240 TPS son 25 ms, aproximadamente tres fotogramas a 120 FPS. Contar fotogramas visibles a otra tasa no es contar ticks físicos. El margen se prueba variando un input y manteniendo los demás fijos; no representa todas las combinaciones posibles. El signo `+` indica un límite de búsqueda, no una ventana totalmente medida. Las liberaciones relevantes requieren una nueva grabación para conocer su modo.

## v0.6.0 / 2026-11-10 / 01:50 UTC / GD 2.2081 / Geode 5.11.2

-Correcciones:
- La migración valida y copia las grabaciones antiguas sin eliminarlas. Las versiones de un nivel se separan por fingerprint.

-Mejoras:
- Inputs, trayectoria y márgenes accesibles por nivel. Los informes sin cambios no se reescriben durante la misma sesión.
- La caché de informes guarda hashes en lugar de duplicar en memoria las trayectorias CSV.

-Cambios:
- Ubicación activa: `recordings/<nombre>-<id>/<fingerprint>/capture.cfr`.
- Los CSV son derivados: editarlos no modifica la reproducción. Esta versión conserva el análisis exhaustivo desde cero; el nuevo almacenamiento no lo acelera por sí solo.

-Implementaciones:
- `metadata.json`: esquema, fingerprint, compatibilidad, TPS y estado completo/verificado.
- `capture.cfr`: fuente de verdad de inputs, trayectoria y ventanas, en formato CFR 2.
- `inputs.csv`: tick, pulsación/liberación, botón, jugador y posición.
- `trace.csv`: posiciones muestreadas de ambos jugadores, no un estado completo del motor.
- `windows.csv`: margen temprano/tardío, ancho, límite inferior y milisegundos por input. Los anchos sin medir quedan vacíos.
- `scan-report.txt`: progreso guardado, radio de búsqueda y último estado, sin estimación de tiempo restante.
- `.bak` guarda la versión anterior; `.tmp` es un archivo transitorio.

Los `.cfpack` conservan su formato e incluyen las capturas completas. Los informes se regeneran al abrir el nivel importado. Las grabaciones anteriores en `levels/` y en la raíz se copian al abrir su nivel; los originales quedan intactos. Export incluye también las versiones anteriores del mismo nivel en `levels/`.

Plan de aceleración: buscar desde el input original hacia cada lado hasta el primer fallo, después restaurar estados completos cerca del input en vez de reiniciar desde cero. Los checkpoints de práctica y trace.csv por sí solos no bastan para restaurar triggers, objetos y física. Comparar cada optimización con el análisis exhaustivo antes de activarla.

## v0.5.2 / 2026-11-10 / 01:25 UTC / GD 2.2081 / Geode 5.11.2

-Correcciones:
- Se bloquea la solicitud automática de checkpoints del jugador durante Verify, Analyze y Play, antes de entrar al código de colocación.
- Se eliminan los hooks que devolvían nullptr desde createCheckpoint y markCheckpoint: otros mods pueden esperar un objeto válido. Esto corrige una posible causa del acceso nulo mostrado dentro de Mega Hack; requiere prueba con los mods del dispositivo.
- La música se silencia por volumen durante Verify y Analyze, incluso si los reinicios intentan restaurarlo. Se recupera el volumen previo al pausar, detener o salir.

-Mejoras:
- La grabación en práctica conserva los checkpoints normales.

-Cambios:
- La música continúa avanzando en silencio durante el análisis, sin llamadas globales a resumeAllMusic que pudieran reanudar canciones pausadas por GD.

-Implementaciones:
- Protección en tryPlaceCheckpoint y updateCheckpointTest, limitada a los jugadores de la sesión automática.
- Versión visible del menú sincronizada con el paquete.
- En Windows las funciones inline no admiten hooks: se bloquea tryPlaceCheckpoint y se reaplica el silencio después del reinicio y en cada actualización. Android intercepta también updateCheckpointTest y los cambios de volumen.

## v0.5.1 / 2026-11-10 / GD 2.2081 / Geode 5.11.2

-Correcciones:
- Verify y Analyze silencian la música de fondo mientras se ejecutan y la restauran al pausar, detener o terminar.

-Mejoras:
- Los sonidos de clicks del mod siguen activos durante el análisis y el showcase conserva su audio normal.

-Cambios:
- La música ya no participa en el ciclo de audio de una verificación automática.

-Implementaciones:
- Restauración segura de la música incluso si cambia la escena durante Verify.

La grabación `run-1539950219185091717.cfr` comienza con el primer click en el tick 104; el fallo del tick 214 sucede después del primer click. La música no cambia la física, pero se silencia para eliminar carga y sincronización de audio durante Verify. Si sigue muriendo en el mismo tick, la causa es una divergencia de reproducción del nivel o de otro mod.

## v0.5.0 / 2026-11-10 / 00:49 UTC / GD 2.2081 / Geode 5.11.2

-Correcciones:
- El análisis comprueba la muerte real después de `destroyPlayer`, en lugar de fallar ante cualquier solicitud de colisión. Ignora como causa de fallo al jugador secundario fuera de dual y las llamadas durante reinicios.
- Diagnóstico de muerte con tick, jugador y primer input en el log. La grabación adjunta al reporte tiene su primer input en el tick 198; el aviso del tick 2 requiere comprobarse de nuevo en el dispositivo.
- Se conservan la restauración de velocidad/visibilidad, las comprobaciones de paquetes y la invalidación de márgenes al desplazar la run de v0.4.0.

-Mejoras:
- Contador por filas de colores según el ancho real de las ventanas y resumen equivalente en el menú.
- Degradado de rojo para ventanas estrechas a verde para ventanas amplias.

-Cambios:
- `makeRows` incluye todos los anchos desde el mínimo medido hasta el máximo, con filas vacías intermedias. No muestra categorías inferiores al mínimo del nivel.
- La última fila agrupa los anchos restantes como `N+` al alcanzar el límite de filas; el máximo también muestra `+` si es un límite inferior.
- Se eliminan las comprobaciones del ZIP que exigían omitir huecos; se mantienen las pruebas válidas y se añaden casos para intervalos y límites abiertos.

-Implementaciones:
- Ajuste Max counter rows (2–12, por defecto 8).
- Integración de las funciones del ZIP v0.5.0 sobre la versión actual del repositorio.

## v0.4.0 / 2026-11-10 / 00:26 UTC / GD 2.2081 / Geode 5.11.2

-Correcciones:
- La verificación reintenta con desfases 0, +1, -1, +2 y -2 ticks antes de fallar; los desvíos de posición se registran como advertencia.
- Si cambia el desfase de la run, se descartan los márgenes anteriores y se vuelven a medir. Se eliminan las liberaciones posteriores al final que no llegaron a reproducirse.
- Al pausar, detener o salir del análisis se restauran la velocidad previa y la visibilidad del nivel; un cambio de escena también libera la velocidad global.
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
