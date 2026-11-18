# Configuración de entrenamiento: 1.0.21

Instala Click Frames GD 1.0.21 y Click Frames Collector 0.1.4, en GD 2.2091
con Geode 6. Desactiva CBF, ignore damage y modificaciones de física. En Tools deja Noclip apagado y Speedhack en 1.0; el chip de Tools debe decir CLEAN.
Mantén el mismo modo de entrada al grabar y analizar. Las unidades son 240 TPS,
independientes de los FPS de pantalla.

En Frames > Settings > Extra options > All settings configura:

| Opción | Valor |
| --- | --- |
| Grouped window search | Exact |
| Dataset capture mode / Strict training scan | ON |
| Fast local segment analysis / Local analysis | OFF |
| Validate landing in full-run mode | OFF |
| Search radius (ticks) | 20 |
| Local survival horizon (240 Hz ticks) | 0 |
| Audited airborne reconvergence | OFF |
| Probe neighboring flight inputs | OFF |
| Analysis speed | 1.0 |
| Batched analysis | OFF |
| Validated native state cache | ON |
| Event checkpoint targets | 192 |
| Analysis work budget per frame (ms) | 16 |

Este perfil prioriza verificación a cadencia normal. Dataset mode fuerza Exact
y Full run incluso si las otras opciones de alcance quedaron activadas.
Los controles de pantalla y sonido no cambian las etiquetas.

Para activar el colector, ve a Settings > Extra options > Collect normal runs
y acepta las DOS confirmaciones. En los ajustes del Collector:

| Opción | Valor |
| --- | --- |
| Collect normal-mode attempts | ON |
| Trajectory sample stride | 1 |
| Maximum samples per attempt | 120000 |

120000 muestras equivalen a unos 500 segundos a 240 TPS. Para niveles más
largos aumenta el límite antes de jugar, hasta 500000; usa memoria adicional.
Comprueba `samples_truncated` en los metadatos antes de entrenar con trayectorias.

Juega manualmente en NORMAL con Speedhack en `1.0x`: el colector guarda todos
los intentos limpios y además una copia de cada 100% en `normal-attempts/` y
`completed-runs/`. Si Speedhack está activado en modo normal con el colector
activo, el colector bloquea automáticamente la grabación y muestra un aviso una
vez por sesión explicando por qué no puede usarse Speedhack para el colector
(altera el tiempo de reacción humano). Si activas Noclip o Speedhack a mitad de
intento, el colector descarta ese intento sin escribir archivos ni entradas en
`index.tsv` (los intentos antiguos con `result = mode-change` deben ignorarse).
En el apartado de análisis de ventanas (`1. RECORD`), el Speedhack sí está
permitido y solo se verifica que no se use Noclip (ni Ignore Damage o Start Pos).

Para obtener etiquetas de ventanas, usa Record en Click Frames, termina la
grabación y pulsa Analyze con el perfil anterior. La recolección normal por sí
sola no mide ventanas. Al terminar Verify, `verify-summary.txt` resume junto a
`analysis-policy.txt` si la referencia fue estable (`baseline_restarts`,
`shift_ticks`, `batch_disabled`, `tolerated_event_ticks`, `end_tick`). Al
guardar/finalizar, el analizador actualiza:

- `dataset_entrenamiento.csv`: filas verificadas, cerradas, no estimadas y con
  bordes confirmados por colisión y captura física compatible.
- `dataset_abiertas.csv`: ventanas abiertas, separadas de las etiquetas cerradas.
- `dataset_manifest.json` y `dataset-report.txt`: cantidades y exclusiones.
- `recordings/`: capturas, análisis, `verify-summary.txt` y diagnósticos.

Estas rutas pertenecen a la carpeta de guardado de `juan.click_frames_gd`.
Las carpetas del colector están en la de `juan.click_frames_collector`.
En Android normalmente están bajo `geode/mods/<id>/`; usa la carpeta de datos
que muestre tu instalación de Geode para localizar el directorio real.

Antes de entrenar, ejecuta `python3 tools/dataset_check.py dataset_entrenamiento.csv`
para revisar filas por nivel (`digest_nivel`), modo (`vehiculo`) y ancho
(`TARGET_frames`), comprobar que no haya duplicados, verificar el umbral mínimo
(≥1000 filas cerradas y ≥10 niveles) y obtener una partición
entrenamiento/validación agrupada por nivel.
