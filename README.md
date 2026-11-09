# Click Frames GD

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

El margen es el intervalo continuo de ticks válidos alrededor del click original, manteniendo los demás inputs fijos. Una única posición válida (desplazamiento de cero frames) pertenece a 1F / frame perfect. El signo `+` indica que la ventana alcanza el límite de búsqueda y podría ser mayor. El análisis depende de la reproducción de la run: si esta diverge, se detiene e informa del error.

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
