# Estado del prototipo

Se añadió un acceso Click Frames en pausa con Record, Play / Replay,
Settings, Stop y Close. Record reinicia el nivel y activa práctica.
Al reiniciar desde un checkpoint se conservan eventos anteriores al tick
restaurado. Esto necesita validación en GD, especialmente con botones
mantenidos, checkpoints automáticos y dual.

El HUD muestra intervalos entre pulsaciones, NO ventanas de supervivencia.
No hay todavía detector de frame perfects ni análisis automático.

Para implementar el analizador:
1. Validar que la grabación completa se reproduce sin morir desde el inicio.
2. Para cada entrada, repetir la run desplazándola ticks antes y después.
3. Registrar éxito, muerte, timeout y orden válido de pulsación/liberación.
4. Medir el intervalo continuo alrededor del tick original y marcar los
   resultados que alcanzan el límite de búsqueda como cotas inferiores.
5. Guardar resultados asociados al contenido del nivel y a la grabación.
6. Mostrar ventanas durante el showcase y contar únicamente resultados
   medidos. Un intervalo entre clics de un tick no es un frame perfect.

Limitaciones actuales: archivos identificados por level ID (niveles locales
con ID cero pueden colisionar), sin validación de replay, sin restauración
explícita de estados de botones en checkpoints. No se ha compilado ni probado
en GD: el entorno de edición no dispone de CMake ni Geode SDK.

La compilación existente de GitHub Actions se conserva para validación futura.
