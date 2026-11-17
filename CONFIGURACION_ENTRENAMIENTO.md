# Configuración de entrenamiento: 1.0.16

Instala Click Frames GD 1.0.16 y Click Frames Collector 0.1.1, en GD 2.2091
con Geode 6. Desactiva CBF, ignore damage/noclip y modificaciones de física.
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

Juega manualmente en NORMAL: el colector guarda todos los intentos y además
una copia de cada 100% en `normal-attempts/` y `completed-runs/`. Las partidas
normales cuentan en las estadísticas habituales. Las pruebas y Play automáticos
se excluyen de la recolección. Evita duplicar los 100% al mezclar ambas carpetas:
usa `attempt_id` para identificarlos.

Para obtener etiquetas de ventanas, usa Record en Click Frames, termina la
grabación y pulsa Analyze con el perfil anterior. La recolección normal por sí
sola no mide ventanas. Al guardar/finalizar, el analizador actualiza:

- `dataset_entrenamiento.csv`: filas verificadas, cerradas, no estimadas y con
  bordes confirmados por colisión y captura física compatible.
- `dataset_abiertas.csv`: ventanas abiertas, separadas de las etiquetas cerradas.
- `dataset_manifest.json` y `dataset-report.txt`: cantidades y exclusiones.
- `recordings/`: capturas, análisis y diagnósticos.

Estas rutas pertenecen a la carpeta de guardado de `juan.click_frames_gd`.
Las dos carpetas del colector están en la de `juan.click_frames_collector`.
En Android normalmente están bajo `geode/mods/<id>/`; usa la carpeta de datos
que muestre tu instalación de Geode para localizar el directorio real.

Verifica que el CSV tenga filas y revisa las exclusiones del informe. Un análisis
fallido o una ventana estimada/abierta no produce una etiqueta cerrada válida.
Los datos del colector son trayectorias e inputs crudos para preparar el dataset;
no entrenan una red automáticamente.
