# Click Frames GD

## v1.0.4 / 2026-11-11 / 12:10 UTC / GD 2.2081 / Geode 5.11.2

-Correcciones:
- Conserva las correcciones de 1.0.3: broadcasts generales separados de triggers despachados, copia retenida de colisiones y comparacion de estado fisico sin rotacion/escala visual ni P2 inactivo.
- No convierte una diferencia de altura/modo en muerte ni salta huecos de la ventana. Full run sigue certificando hasta el final.

-Mejoras:
- Tablas densas por tick y una observacion reutilizada por callback. La captura de trayectoria de candidatos respeta su opcion; el lote evita actualizar el HUD o el motor de precision en cada slice.
- Checkpoints dirigidos por eventos en la ultima referencia, con presupuesto configurable, limites de mapas/objetos, descarte cronologico y fallback a un estado anterior antes del reinicio completo.
- Hasta 1024 slices por lote con presupuesto configurable (12 ms por defecto). Las confirmaciones pueden usar lotes despues de pasar la referencia; se conserva el fallback automatico de cadencia.
- Busqueda contigua por etapas desde radio 4. Local puede detener ventanas amplias en 8+ por defecto; poner Broad window lower-bound cap en 0 conserva la busqueda hasta el radio completo. N+ siempre es un minimo, no un margen exacto.

-Cambios:
- Politica CF-ANALYSIS-4 con invalidacion por ajustes de reconvergencia/cap. Captura, playback y archivos Edit conservan sus formatos.
- Reconvergencia local en suelo o aire exige igualdad exacta del estado fisico no visual, futuro de inputs sin cambios, RNG, historia causal, contactos/flags adicionales y mundo sin efectos dinamicos activos. No equivale a un snapshot universal.
- Una de cada diez aceptaciones tempranas se audita desde cero hasta el final. Si no coincide, se borran las ventanas locales de la sesion y se recalcula con continuaciones completas y sin atajos.
- Si un candidato desde checkpoint discrepa de su confirmacion desde cero, se desactiva la cache para esa sesion, se invalidan las ventanas afectadas y se recalcula desde el inicio.
- La cache solo reutiliza el mismo evento/offset/alcance en la misma sesion. No equipara aterrizajes parecidos ni usa galope que pueda ocultar un hueco.

-Implementaciones:
- Modelo estadistico L* adaptado de Frame Window Counter (MIT hyper-5), independiente de Geode: Base, Nerve, Fatigue, CPS y combinaciones, LUT erfc, secante acotada/biseccion, cancelacion y trabajador propietario sin objetos del juego.
- Settings > Statistical precision counter muestra por defecto sigma en ms (1000/L*), con posicion, ponderacion, unidad, tiempo objetivo, respawn y coeficientes configurables. Es una estimacion estadistica de dispersion temporal; no mide latencia ni sustituye el intervalo entre clicks existente. Las ventanas N+ producen estimaciones conservadoras y Edit se identifica como Edited.
- precision.csv / precision-visual.csv comparan intervalo, holgura temporal y las ocho metricas por prefijo; precision-report.txt guarda parametros y procedencia. Se incluye licenses/frame-window-counter.MIT.txt.
- scan-events.tsv, scan-phases.csv y scan-report.txt registran ensayos, callbacks por fase, ratios, auditorias, cache, costes de reset y causas de fallback.

Pruebas: nueve suites y benchmarks sinteticos (ver BENCHMARK_104.md). No se han medido aun GD, un movil ni la igualdad ON/OFF en tres niveles: no se promete tres segundos, cinco minutos ni una mejora de 10x. Los criterios de dispositivo del plan quedan pendientes de esos reportes reales.

## v1.0.3 / 2026-11-10 / 21:58 UTC / GD 2.2081 / Geode 5.11.2

-Correcciones:
- El antiguo kind 6 mezclaba broadcasts de gameEventTriggered (contactos/materiales incluidos) con activaciones causales. Ya no participa en la paridad; los triggers de evento despachados por activateEventTrigger se registran como kind 8.
- Las diferencias causales aceleradas tienen el mismo reintento sin batching que las fisicas, mediante un estado explicito. La paridad fisica, orbes, pads, teletransportes, grupos y pickups permanecen comprobados.
- No se registran nuevos eventos despues de que el ensayo tenga un resultado pendiente, evitando sobrescribir el primer fallo.

-Mejoras:
- baseline-difference.txt muestra la entrada esperada y la observada con tick, clase, objeto, jugador y detalle. Tambien identifica una referencia agotada. El numero del aviso anterior era el indice de la entrada, no el ID de GJGameEvent.

-Cambios:
- Politica CF-ANALYSIS-3: Analyze invalida resultados de la politica anterior. Los formatos de captura y Edit no cambian.
- No se ignoran arbitrariamente los primeros ticks ni se aprueban ensayos con fisica divergente. La imagen original no permite saber que GJGameEvent/material se comparo porque el aviso anterior no los mostraba.

-Implementaciones:
- raw-game-signals.tsv conserva hasta 64 avisos por intento (maximo 64 KiB por sesion) con event, material y playerID original, solo como diagnostico.
- Pruebas de separacion de kind 6/8, diferencias reales y diagnosticos esperado/observado. Verificar el mismo nivel en Android sigue siendo necesario.

## v1.0.2 / 2026-11-10 / 21:48 UTC / GD 2.2081 / Geode 5.11.2

-Correcciones:
- Elimina el crash en captureCollisions: CCDictionary::createWithDictionary hace copias profundas virtuales de los valores; los objetos de colision de GD no tienen por que admitir copy(). Una copia nula terminaba en CCObject::retain.
- La captura y la restauracion ahora copian la pertenencia al diccionario reteniendo las identidades originales, sin clonar objetos del juego. Se conservan claves enteras o texto y se omiten valores nulos.

-Mejoras:
- Restaurar conserva la direccion del diccionario del jugador, evitando invalidar referencias que otros componentes puedan mantener. La instantanea conserva su propio conjunto de entradas.

-Cambios:
- Se mantiene la validacion fisica y causal de cada estado. Los valores son referencias retenidas, no una copia independiente de objetos mutables ni un snapshot universal.
- No se atribuye el fallo a Mega Hack, Versus o FrameLab: la ruta que provoca este crash estaba en nuestro codigo de copia de diccionarios.

-Implementaciones:
- RetainedDictionary.hpp y una suite de regresion para valores no copiables, retencion, claves enteras/texto, identidad de destino, alias y casos vacios/nulos. Compilacion y pruebas no sustituyen reproducir el caso en Android.

## v1.0.1 / 2026-11-10 / 21:25 UTC / GD 2.2081 / Geode 5.11.2

-Correcciones:
- La paridad compara tamano fisico y velocidad del jugador en vez de escala/rotacion visual; ignora P2 cuando dual esta inactivo. Tolera solo 0.0001 unidades en coordenadas y 1e-8 en otros escalares, rechazando valores fisicos no finitos.
- Los lotes repiten la secuencia de dt de la referencia, conservando la agrupacion de subpasos. Se consumen resultados al terminar una actualizacion antes de iniciar la siguiente.
- Un aviso de inestabilidad incluye campo, valor original y valor observado; baseline-difference.txt y scan-report.txt conservan el diagnostico. La imagen por si sola no identifica el campo que fallo en 1.0.0.

-Mejoras:
- Snapshots ampliados con copias tipadas de mapas de fisica/tweens, eventos, efectos de entrada/salida, acciones dinamicas y estados de audio, junto con relojes, semilla RNG global, diccionarios de colision por jugador y cursores de cadencia/traza causal. Se complementa el checkpoint nativo; no se copia GJGameState como bloque de memoria.
- Tres referencias verifican tambien el orden y tick de las interacciones instrumentadas. Las restauraciones deben reproducir esa traza; una discrepancia rechaza el checkpoint.

-Cambios:
- Politica CF-ANALYSIS-2: Analyze recalcula ventanas de politicas anteriores.
- Flight recovery prueba -1/+1 tick sobre el siguiente input del mismo jugador a no mas de 240 ticks. Es evidencia de rutas alternativas; no aumenta el contador de margenes de la macro fija. Puede desactivarse en Settings.

-Implementaciones:
- Hooks de orbes (ringJump), triggers tocados, pads, teletransportes, toggles de grupos, eventos de juego y pickups. baseline-causes.tsv guarda tick, clase de evento, ID de objeto, jugador y detalle.
- flight-recoveries.tsv guarda los ensayos alternativos. Los inputs y ediciones originales permanecen separados.
- Alcance: esta instrumentacion no cubre todos los overrides de triggers ni constituye una prueba universal de todo el motor. Los snapshots ampliados siguen dependiendo del checkpoint nativo. Geometria analitica Wave y adaptador CBF fraccionario siguen pendientes; el CBF publico inspeccionado usa una cola privada sin API de replay publicada. No se habilita soporte fraccionario aproximado.

Validacion en dispositivo necesaria: volver a analizar FRPRUEBA, comprobar si pasa la referencia y comparar dos analisis completos; si diverge, compartir baseline-difference.txt con el capture de ese nivel. Desactivar Batched analysis y Native state cache permite aislar conflictos de mods sin ocultar divergencias reales.

## v1.0.0 / 2026-11-10 / 20:56 UTC / GD 2.2081 / Geode 5.11.2

-Correcciones:
- Timeout, bloqueo y muerte anterior tanto al input original como al desplazado dejan la ventana sin medir. Morir esperando un salto retrasado sigue siendo fallo fisico valido. Una zona local distinta ya no se convierte automaticamente en fallo fisico.
- Verify parte de capture.flr en cada analisis; la alineacion se aplica solo al playback. Las medidas previas se invalidan si cambia esa alineacion.
- Los taps de cubo/bola/arana conservan su duracion desplazando press y release del mismo modo, sin cruzar el siguiente input. Robot y vuelo conservan la liberacion independiente. Es una politica de perturbacion explicita, no una medida de press con hold fijo.

-Mejoras:
- Scheduler por lotes de hasta 256 slices, con presupuesto de 8 ms por pantalla. Conserva el orden normal de actualizaciones y hooks; el nivel permanece oculto entre slices.
- Tres referencias comparan por tick coordenadas, velocidad vertical, escala, rotacion, modo, gravedad, suelo, dual y botones. La primera corre con cadencia normal; si los lotes divergen, se reconstruye sin aceleracion.
- Cache nativa activada por defecto: hasta 64 entradas, limites de estados de objetos y validacion de continuacion fisica antes de reutilizar. Las fronteras estrechas fallidas se confirman desde cero sin batching.

-Cambios:
- El alcance LOCAL sigue siendo local: acepta reconvergencia observable durante cuatro ticks de suelo o el horizonte configurado. No certifica igualdad de todo el estado oculto de triggers ni el resto de la run. FULL conserva la comprobacion hasta el final.
- Los resultados anteriores a esta politica se recalculan al pulsar Analyze. Los de la nueva politica se pueden reanudar; Edit y sus archivos permanecen separados.
- La aceleracion afecta al scheduler global mientras se analiza; otros mods pueden impedir la paridad. Desactivar Batched analysis permite comparar el comportamiento.

-Implementaciones:
- Comparacion fisica exacta de observaciones finitas, fallback de cadencia, errores tecnicos separados y marca analysis-policy.txt.
- Alcance pendiente del plan amplio: snapshots universales independientes de checkpoints, traza causal completa de orbes/triggers, geometria analitica Wave, recuperacion de inputs de vuelo y adaptador CBF fraccionario. Esta version no implementa esas partes ni garantiza tres segundos.

Prueba de dispositivo necesaria: repetir un analisis nuevo con el mismo capture, comparar ventanas FULL/LOCAL y tiempos, pausar/cancelar, comprobar restauracion de musica y probar Play/Edit hasta finalizacion nativa. Compilar no prueba rendimiento ni determinismo dentro de GD.

## v0.10.1 / 2026-11-10 / 20:17 UTC / GD 2.2081 / Geode 5.11.2

-Correcciones:
- Un showcase completado correctamente usa PlayLayer::levelComplete y la pantalla nativa correspondiente al modo actual, sin reinicio ni pausa forzada.
- La finalizacion limpia los overlays del mod y libera sus inputs inyectados antes de mostrar el resultado. Reiniciar permite volver a mostrar los contadores en el siguiente Play.

-Mejoras:
- La misma salida se aplica al showcase medido y al editado.

-Cambios:
- Record/Analyze mantienen su retorno a pausa; una reproduccion fallida conserva el aviso de error. Se mantiene practica: no se cambia de modo para conceder una finalizacion normal.
- Esta correccion no cambia el algoritmo de medicion ni promete tiempos de tres segundos.

-Implementaciones:
- Pruebas de fin de inputs: admite liberaciones finales pendientes, rechaza pulsaciones sin reproducir y cursores invalidos.

Regresion en dispositivo: completar Play medido y editado, verificar que aparece el resultado nativo sin volver al inicio, reiniciar y comprobar el siguiente Play, y confirmar que Verify/Analyze conservan su flujo. La exactitud/reproducibilidad de margenes sigue pendiente de diagnostico con capture.flr, playback.flp y scan-report.txt; coincidir con otro contador en un salto no valida todas las ventanas.

## v0.10.0 / 2026-11-10 / 18:08 UTC / GD 2.2081 / Geode 5.11.2

-Correcciones:
- La migracion valida los .cfr de las tres ubicaciones anteriores y conserva los originales. Un bundle corrupto bloquea el guardado automatico para evitar sustituirlo por una captura vacia; Record permite reemplazarlo explicitamente.
- capture.flr y playback.flp quedan vinculados por hash. Una escritura interrumpida puede recuperar el playback anterior compatible desde .bak; los resultados de otra captura no se aplican.
- Los aterrizajes se observan en los pasos fisicos, evitando depender de que la pantalla dibuje el instante de contacto.
- Los informes de rendimiento se conservan al reabrir el nivel, en vez de sustituirlos por contadores a cero.

-Mejoras:
- Guardado incremental: al medir otro evento solo se actualiza playback.flp y el informe pequeno. No se serializa ni se reescribe la trayectoria completa en cada evento.
- Fast local segment analysis: termina el candidato al alcanzar un aterrizaje de referencia o la salida de su tramo. No sigue hasta el final del nivel en los casos con referencia utilizable.
- La cache nativa experimental captura hasta ocho estados durante Verify y los comparte entre eventos. Cada estado se valida una vez contra la ruta completa; su primer fallo de frontera se confirma desde cero. Se rechaza la cache si esa confirmacion discrepa. Sigue desactivada por defecto y no prueba equivalencia universal de todos los estados del motor.

-Cambios:
- Se mantiene `recordings/<nombre>-<ID>/<fingerprint>/`. Las extensiones son formatos propios de Click Frames, no formatos compatibles con otro mod por compartir nombre.
- Settings > Fast local segment analysis esta activado por defecto. Al desactivarlo se obtienen margenes de run completa. Cambiar alcance reinicia las medidas al pulsar Analyze; cargar una captura no elimina sus resultados previos.
- El menu diferencia LOCAL WINDOWS, FULL RUN WINDOWS y VISUAL WINDOWS. El analisis local conserva los demas inputs grabados, pero no certifica consecuencias posteriores al tramo. Sin referencia precisa se usa la ruta completa como fallback.
- Los paquetes con ediciones usan CFPACK 2. Se siguen importando CFPACK 1 y CFR 2/3/4; el transporte de resultados locales usa CFR 5 y requiere esta version. Los paquetes sin ediciones conservan la version 1 del contenedor.

-Implementaciones:
- `capture.flr` (CFCAP 1): inputs, modos, perfil de entrada, final y trayectoria originales. Solo cambia al grabar/guardar una captura; los desplazamientos de Verify van a playback.flp.
- `playback.flp` (CFPLAY 1): tiempos normalizados, ventanas, radio, politica y progreso verificado, ligados al hash de la captura. No duplica la trayectoria.
- `capture-trace.csv`: vista de la trayectoria, generada cuando cambia la captura o se migra. `capture-rings.tsv` se reserva con cabecera explicita: todavia no hay registro de interacciones con orbes, ni se inventan datos.
- `edits-<hash-trayectoria>.tsv`: convierte el JSON del editor sin borrarlo, conserva todos sus campos y viaja con el .cfpack nuevo. El editor, sus controles, undo/redo, conteo y Play editado se mantienen. El formato numerico usa 0/1 para booleanos.
- `metadata.json` y `prediction-trails.txt` siguen siendo auxiliares. Los trazos grandes se guardan al detener/completar/salir, no por cada evento. Las vistas antiguas CSV y el JSON permanecen como archivos heredados y ya no son la fuente activa.
- saved.json/settings.json siguen gestionados por Geode; solo se actualizan claves de ultima carpeta/captura y esquema, sin sobrescribir preferencias del usuario.
- scan-report.txt incluye segundos activos, callbacks fisicos, candidatos, restauraciones, fallbacks y estados compartidos. No promete un tiempo restante.

### Probar la migracion y el analisis rapido

1. Abre un nivel con una captura guardada. Se crean los nuevos archivos sin borrar capture.cfr ni showcase-edits.json.
2. Comprueba Frames > EDITAR y Play. Las ediciones quedan vinculadas al mismo recorrido; exportar/importar tambien las conserva en CFPACK 2.
3. Para medidas rapidas usa Fast local segment analysis y pulsa Analyze. LOCAL mide el mismo aterrizaje (plataforma y corredor 60x6 unidades) o el estado anterior a la siguiente accion relevante del mismo jugador (corredor 60x30, modo y gravedad). Cambios que solo funcionan en otra ruta pueden rechazarse por ese criterio.
4. Para acelerar tambien los prefijos, prueba Experimental shared native state cache en un nivel corto y compara resultados. Desactivarlo fuerza reinicios completos. Una cache fallida se descarta; los limites son ocho estados y 25000 registros de objetos por estado, sin contar toda la memoria privada del motor/mods.
5. Revisa scan-report.txt. Esta primera mejora no incluye un bucle headless de fisica ni garantiza cinco minutos en cualquier nivel. La duracion debe medirse en Android con el nivel y sus mods.

Pruebas host: migracion idempotente, captura inmutable entre ventanas, ajustes de tiempos, recuperacion de escrituras interrumpidas, hashes incompatibles, TSV/JSON del editor, paquetes viejos/nuevos y salidas de tramos. Con las dos grabaciones aportadas se verifico que el .cfr original queda identico y la carga reconstruida conserva sus datos: payload de progreso de 284250 a 4673 bytes y de 86423 a 2106 bytes. Es una medida de bytes, no un benchmark de tiempo del motor.

## v0.9.0 / 2026-11-10 / 03:15 UTC / GD 2.2081 / Geode 5.11.2

-Correcciones:
- El editor no altera los inputs ni las ventanas medidas. La presentacion visual se vincula al fingerprint del nivel y al hash de la trayectoria: un archivo de otra captura no se aplica.
- Los saltos espaciales de mas de 256 unidades no generan puntos intermedios ficticios en el editor ni cadenas a traves del teletransporte.
- Se evita copiar toda la trayectoria al construir los contadores del menu y del showcase.
- Inicializacion explicita de la sesion para admitir los contenedores de colas de GD en Android.

-Mejoras:
- Editor con trayectoria esquematica, barra de tiempo, pasos de 1/24 ticks, zoom, seleccion cronologica, pincel, cadenas, deshacer/rehacer y guardado.
- Circulos anclados exclusivamente a un momento de la trayectoria de P1/P2; sin arrastre libre ni posiciones arbitrarias.

-Cambios:
- El menu identifica el modo EDITADO. Durante Play se usan los numeros visuales y su contador, conservando los inputs reales y el indicador de intervalo entre clicks. No se superponen las previews medidas a una presentacion editada.
- Play editado admite una captura completa sin medir todos sus margenes. La reproduccion aun debe ser estable; editar circulos no corrige inputs ni muertes.
- .cfpack sigue exportando la captura original, sin showcase-edits.json. Para compartir la presentacion hay que copiar tambien el JSON a la carpeta de la misma captura/trayectoria.

-Implementaciones:
- `showcase-edits.json`: esquema 1, fingerprint, hash de trayectoria, enabled y marcadores con tick, width (1-120), color (0 auto, 1 rojo, 2 amarillo, 3 cian, 4 verde, 5 blanco), scale (0.5-2), count, sound y p1. Los booleanos son JSON true/false. El lector exige el esquema/orden generado por el editor y valida limites antes de cargarlo.
- Cadenas por distancia sobre el recorrido muestreado; Paso alterna 10/20/40/80 unidades del nivel. No son centimetros de pantalla. Maximo 5000 marcadores y 32 pasos de historial.
- La cache experimental de GD ahora conserva tambien las colas de entrada, semillas aleatorias, paso actual, acumulador delta y temporizador de reanudacion alrededor del checkpoint nativo, manteniendo su validacion de ida/vuelta y fallback.
- Pruebas del JSON, identidad de trayectoria, interpolacion, teletransportes, cadenas, conteo visual e historial.

### Usar el editor

1. Guarda una run y abre Pausa > Frames > EDITAR. No es un video ni un editor completo del escenario: muestra la trayectoria grabada.
2. Elige el punto con la barra o -24/-1/+1/+24 y pulsa Anadir. F-/F+, Color, Tam, Cuenta, Audio y Jugador ajustan el marcador seleccionado o el pincel para el siguiente.
3. Anterior/Siguiente seleccionan marcadores. Mueve la barra y pulsa Mover para reubicar el seleccionado sobre la trayectoria. Eliminar borra el seleccionado.
4. Para una cadena: pulsa Inicio, mueve la barra al final, elige Paso y pulsa Cadena. Los circulos usan los valores del pincel.
5. Guardar aplica y escribe el JSON con backup. X descarta lo no guardado. Reabre Frames > Play para verlo sobre el nivel real.
6. Para volver a los margenes medidos: cambia Editado a NO y guarda. Deshacer/Rehacer solo afectan el borrador del editor.

### Alcance real de los pendientes de fisica

El objetivo de estados es Geometry Dash, no el estado privado de otros mods. La implementacion sigue siendo la cache nativa experimental, ahora ampliada con estado adicional del motor; no se declara universal sin verificar triggers, objetos dinamicos y transiciones en el dispositivo. Se mantiene desactivada por defecto.

No se ha completado la prediccion en vivo para inputs manuales nuevos ni la entrada fraccionaria de CBF. La preview medida y CoS de 240 TPS de v0.8.0 se conservan. El adaptador sub-tick y la simulacion aislada siguen pendientes; esta version no redondea CBF a ticks para presentarlo como compatible.

## v0.8.0 / 2026-11-10 / 02:34 UTC / GD 2.2081 / Geode 5.11.2

-Correcciones:
- Búsqueda desde el input original: -1, -2... hasta el primer fallo; después +1, +2... hasta el primer fallo. Conserva el intervalo continuo del método exhaustivo y deja de probar islas desconectadas.
- Los inputs conservan su identidad al ordenar candidatos para dual/botones independientes.
- CFR 4 guarda la política de análisis y el perfil vanilla240/cos240; lee CFR 2/3. Un cambio de política invalida márgenes anteriores.

-Mejoras:
- Validate reference landing: en saltos de cubo/robot con aterrizaje observado en Verify, el primer aterrizaje debe coincidir en plataforma y quedar dentro de 60 unidades horizontales / 6 verticales de la referencia. Además debe terminar la run. No identifica semánticamente todos los obstáculos; los demás casos conservan la validación de ruta completa.
- Hasta tres trazos por evento (un éxito y el primer fallo de cada lado) se muestran durante Play. Verde significa candidato que pasó la prueba y rojo candidato rechazado; la cruz señala el final de rechazo capturado, que puede ser una muerte o un aterrizaje fuera de la zona.

-Cambios:
- La vista previa usa simulaciones de candidatos ya ejecutadas y la secuencia grabada. No predice inputs manuales nuevos, no se calcula en vivo antes del análisis y puede truncarse por límites de memoria/tiempo.
- Native state cache es experimental y está desactivado por defecto. Usa el estado nativo del checkpoint de GD; no es una copia completa del proceso ni del estado privado de los otros mods.
- Los archivos nuevos requieren esta versión: los lectores antiguos no admiten CFR 4. Las capturas anteriores se conservan como backup al reemplazarlas.

-Implementaciones:
- Experimental native state cache: captura antes de la ventana del input, valida una repetición original desde ese estado contra posiciones de ambos jugadores y tick final, y reutiliza el estado en ese evento. Si falla, reinicia desde cero. El primer fallo de una prueba con caché se confirma desde cero antes de fijar el margen. Esta validación no demuestra equivalencia de todo el estado de triggers o de otros mods.
- Analyzed trajectory preview: hasta 256 puntos por rama y 32768 puntos totales. Se guarda en `prediction-trails.txt` con hash del capture.cfr; se descarta si no coincide. Las previews no viajan en .cfpack. Analyze vuelve a medir una run completa importada sin previews para generarlas.
- Perfil CoS de syzzi.click_between_frames: permite Click on Steps con física de 240 TPS y sin Physics Bypass. El perfil debe coincidir entre Record y Analyze/Play; cambiarlo durante la reproducción detiene la tarea. Compatibilidad runtime pendiente de prueba en Android.
- Pruebas del buscador contra todos los patrones hasta radio 6, corredores de aterrizaje, caché de trazos, identidad de inputs y CFR 4.

### Qué queda pendiente del plan completo

CBF fraccionario y CBF Lite siguen bloqueados: almacenar ticks enteros o un reloj de pared no reproduce sus subpasos físicos. Se inspeccionó el código público de [CBF](https://github.com/theyareonit/Click-Between-Frames): subdivide PlayerObject::update y las colisiones, y no expone una interfaz pública de replay para su cola. Hace falta un adaptador de captura e inyección de esos subpasos y pruebas de equivalencia; esta versión no lo sustituye por un reloj aproximado.

También quedan pendientes estados universales independientes de otros mods, validación semántica de cualquier salto y predicción de inputs manuales arbitrarios. Las funciones experimentales de arriba son partes concretas del plan, no una implementación completa de esas capacidades.

### Prueba en dispositivo

Primero analiza un nivel corto con Native state cache desactivado; comprueba ventanas y trazos. Repite con la caché activa y compara resultados. Prueba aterrizajes en plataformas móviles, dual, portales y muertes antes/después del target. Comprueba que Stop, Replay y salir del nivel no reutilizan un estado anterior. Con CBF usa Click on Steps y Physics Bypass desactivado; vuelve a grabar en ese perfil. No hay garantía runtime por el mero hecho de compilar.

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
