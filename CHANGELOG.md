# Registro de cambios — ARTPICST

## [Sin publicar]

### Cierre: robustez del fallback y consistencia
- **El log se escribe en UTF-8 real.** Se usaba `wofstream`, que convierte
`wchar_t` con la faceta "C": los acentos de los mensajes en español se perdían
(o el flujo fallaba en MinGW/libstdc++) y la línea acababa solo en el depurador.
Ahora se compone la línea y se escribe con `ofstream` binario + `WideToUtf8`,
con CRLF.
- **Tras un fallo de GPU se repinta TODA la ventana con GDI+.** Antes solo se
rehacía la región sucia, así que un frame abortado a medias podía dejar media
ventana con contenido viejo o incompleto hasta el siguiente repintado ajeno.
- **Un fallo de subida de textura ya no destruye el render target.** El fallo es
del contenido (VRAM/staging), no del contexto: se descarta solo el bitmap de la
imagen y se conservan target/pinceles, evitando el parpadeo de recrear todo el
contexto cada 1,5 s.
- **Portapapeles: las imágenes grandes ya se copian.** Usaba el límite de
visualización por lado (5000 px), así que una panorámica que ya se puede abrir
fallaba al copiar en silencio; ahora valida solo desbordamiento.
- **Fallos de DirectWrite o de pinceles del tema** se degradan a GDI+ una vez en
lugar de reintentarse en cada frame.

### Eficiencia final: menos trabajo repetido, menos hilos
- **LOD de textura cuantizado a 1/8 de octava.** La identidad del bitmap se
comparaba por float exacto, así que cada muesca de rueda (cada asentamiento del
zoom) reconstruía la textura entera en CPU y la volvía a subir aunque el tamaño
hubiera cambiado un 2 %. Ahora el LOD se ajusta en escalones del ~9 %: la textura
se reutiliza entre muescas y la diferencia (±4,5 %) la absorbe el bilineal de la
GPU, invisible en pantalla.
- **Hilos de remuestreo proporcionales al trabajo.** Se pedían hasta 8 hilos
siempre; ampliar una miniatura gastaba más en crear hilos que en remuestrear.
Ahora se pide uno por cada 400 000 píxeles de salida (máx. núcleos/8, mín. 1), y
con 1 no se crea ninguno.
- **Textos de la interfaz al día**: la guía y el diálogo «Acerca de» anunciaban
«más de 30 formatos» cuando la lista única tiene 51.

### Decodificación y GPU: auditoría completa
- **El ICC no se aplicaba a JPEG/PNG (corregido).** `stb_image` se intentaba
primero y no lee perfiles de color, así que la conversión a sRGB del bloque ICC
solo ocurría en los formatos que stb no entiende (HEIC/AVIF/TIFF/RAW): casi
nunca en un JPEG o PNG de móvil, que es justo donde el perfil importa. Ahora WIC
va primero en los formatos con gestión de color y stb sigue siendo el primero
para GIF animado, TGA, PSD, HDR, PNM e ICO.
- **Las panorámicas de más de 5000 px ya se abren.** El presupuesto de 100 MB se
aplicaba por LADO *durante* la decodificación, así que los tres decodificadores
rechazaban una 12000x3000 y `DownscaleImageIfTooLarge` era código muerto. Hay un
presupuesto de decodificación aparte (256 MB de pico, techo de 16384 px por lado)
y después se reduce con el remuestreo multihilo al límite de carga.
- **Fuera un pase completo de WIC por imagen.** El `IWICBitmapFlipRotator` con
transformación identidad recorría el fotograma entero para devolverlo igual; la
orientación EXIF la aplica el visor.
- **Pre-chequeo de dimensiones en stb (`stbi_info`)** antes de reservar
`ancho*alto*4`, para no intentar gigabytes con una imagen "bomba".
- **GDI+**: copia contigua cuando el stride de origen coincide (una sola copia en
lugar de una por fila).
- **Detección de gráficas por DXGI (nueva).** Se enumeran los adaptadores, se
elige el de más VRAM dedicada, se distingue **integrada** (< 512 MB dedicados)
de **dedicada** y se registra en el log; la guía (F1) muestra GPU, VRAM y el modo
de aceleración activo.
- **El render target se elige según el hardware.** HARDWARE solo si DXGI enumera
un adaptador real; si falla se reintenta una vez con DEFAULT (WARP). Antes se
forzaba HARDWARE y en escritorio remoto, máquinas virtuales o drivers rotos el
visor se quedaba en GDI+ para siempre.
- **Un fallo de GPU ya no se reintenta en cada frame.** `CreateHwndRenderTarget`,
`EndDraw` y `GpuUpdateImage` marcan el fallo y arman el reintento de 1,5 s; antes
cada `WM_PAINT` repetía la creación del target o el remuestreo completo.
- **Presupuesto de textura por tipo de GPU**: 8 MP en integrada (comparte RAM y
tiene menos ancho de banda de relleno), 12 MP en dedicada.

### Latencia: menos trabajo por frame y temporizadores correctos
- **La animación de zoom corre a 120 Hz de verdad.** Con la resolución de
temporizador por omisión de Windows (~15,6 ms), un `SetTimer` de 8 ms dispara a
~64 Hz: la animación pedía el doble de frames de los que recibía y se veía a
escalones. Se eleva a 1 ms solo mientras dura la animación (`timeBeginPeriod`)
y se restaura al terminar o cancelarla; `winmm` añadido a los cinco pipelines.
- **Fuera una llamada al kernel por frame.** `GpuRenderFrame` llamaba a
`KillTimer(TIMER_GPU_RETRY)` en cada frame pintado aunque no hubiera ningún
reintento armado. Ahora solo se llama si el temporizador está realmente armado
(`retryTimerArmed`), y las rutas que lo cancelan limpian el flag.
- **Ruta de entrada más barata.** En `WM_MOUSEMOVE` el temporizador del dock se
reprograma con un único `SetTimer` (antes `KillTimer` + `SetTimer` por evento,
hasta 1000/s en ratones modernos) y no se hace hit-test del HUD mientras se
arrastra la foto, porque el dock está oculto.
- **El dock vuelve a ocultarse.** Al expirar su temporizador con el puntero
quieto en la franja inferior no se re-armaba: si el usuario dejaba de mover el
ratón, el dock quedaba dibujado hasta un repintado ajeno. Ahora se reprograma
la comprobación.
- **OSD y dock no se redibujan si la región sucia no los toca.** En arrastre o
zoom la franja sucia es la foto; Direct2D recortaba el dibujo, pero se seguían
preparando píldoras y glifos en cada frame.

### Calidad HQ 4K: escalado multihilo y color gestionado
- **Remuestreo de alta calidad repartido entre núcleos.** El escalado que
  alimenta la textura GPU (y la reducción de imágenes que exceden el límite de
  carga) usa ahora `stbir_build_samplers_with_splits` +
  `stbir_resize_extended_split`: los coeficientes se calculan una vez y cada
  hilo escribe una banda disjunta de la salida. Catmull-Rom al ampliar y
  Mitchell al reducir, en espacio lineal sRGB: detalle sin halos inventados.
- **Textura al tamaño exacto de pantalla también al AMPLIAR.** Antes solo se
  remuestreaba al reducir; una foto de 2 MP mostrada en una ventana 4K pasaba
  por el bilineal de Direct2D y se veía blanda. Ahora, en reposo y dentro del
  presupuesto de píxeles, se reconstruye al tamaño de pantalla y se dibuja a
  ~1:1. Cerca de 1:1 (0,98–1,25) no se toca: la textura nativa ya es óptima.
- **Perfiles ICC convertidos a sRGB al decodificar.** Las fotos en Display P3
  o Adobe RGB se mostraban sobresaturadas; WIC aplica ahora el perfil del
  archivo y entrega sRGB. Si el perfil es ilegible o el códec no lo soporta, se
  continúa sin transformar: el color nunca impide abrir la imagen.
- **El búfer de subida ya no aplica el límite de decodificación.** Con el
  límite de carga (5000 px por defecto), una textura ampliada a 4K/8K fallaba en
  `GpuBakePixels` y se perdía el fotograma; ahora solo se comprueba desborde.

### Renderizado GPU: repintados correctos y sin tirones
- **El repintado incremental era incorrecto por contrato.** El target de
  Direct2D se creaba con `D2D1_PRESENT_OPTIONS_NONE`, que *descarta el
  fotograma al presentar*; sin embargo WM_PAINT solo redibujaba la región
  sucia y contaba con que el resto siguiera en el back-buffer. Ahora se crea
  con `D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS`, de modo que mover el ratón por el
  dock, mostrar el OSD o cambiar de estado no deja bandas con contenido viejo.
- **Un `Resize` o un target nuevo tampoco conservan el contenido** (también
  documentado). Se marca el frame como completo (`needsFullRedraw`) para que el
  primer repintado tras crear o redimensionar cubra todo el cliente, y no solo
  la región sucia.
- **Se eliminó una subida de textura de hasta 64 MB al iniciar cada gesto.**
  Durante el arrastre o la animación de zoom se forzaba escala 1.0 (resolución
  completa), lo que re-subía la foto entera en el hilo de UI al empezar el pan
  y volvía a remuestrear al soltar. Ahora se conserva el LOD ya subido: mismo
  aspecto durante el gesto y fotograma nítido al terminar, sin picos de CPU ni
  de ancho de banda.
- **Se respeta el límite de textura del adaptador**
  (`ID2D1RenderTarget::GetMaximumBitmapSize`, 16384 px en una RX 580, 8192 en
  otras GPU). Antes, una imagen mayor hacía fallar `CreateBitmap` en cada
  frame: el visor caía a GDI+ —mucho más lento— y reintentaba el remuestreo en
  cada repintado. Ahora se reduce una vez al límite y se queda en GPU.
- **Geometría 1:1 con el píxel físico en cualquier escala de Windows.** El
  target se creaba con el DPI real del monitor mientras toda la geometría está
  en píxeles de cliente. Direct2D escala cada unidad por `dpi/96`, así que al
  125 % la foto se dibujaba un 25 % más grande (recortada por la ventana) y el
  dock quedaba desplazado respecto de su zona clicable. Ahora el target usa
  96 ppp (1 unidad = 1 píxel): GPU y GDI+ pintan exactamente igual y el zoom
  100 % es 1:1 real.
- **Bordes suavizados en la ruta GPU.** Se fija
  `D2D1_ANTIALIAS_MODE_PER_PRIMITIVE` para que las esquinas redondeadas del
  dock y del HUD tengan el mismo suavizado que la ruta GDI+.
- Higiene: `lastUploadScale` se reinicia al liberar los recursos del target,
  para que ninguna decisión de LOD use un valor de un target anterior.

### Asociaciones de archivo completas y sin efectos colaterales
- **Una sola lista de formatos** (`include/artpicst_formats.hpp`, 51
extensiones): la recorren el instalador (asociaciones y «Abrir con»), el visor
(escaneo de carpetas, filtro del diálogo «Abrir» y `--register`) y la extensión
de shell (miniaturas y vista previa). Antes había tres listas que ya habían
divergido: 51, 49 y 13 formatos.
- **`--register` solo asociaba 13 formatos** y, además, escribía el ProgID como
**valor predeterminado** de cada extensión: secuestraba el visor del usuario
(el instalador nunca lo hacía). Ahora registra los 51 formatos en
`OpenWithProgids`, `Applications\artpicst.exe\SupportedTypes` y
`Capabilities\FileAssociations`, con `RegisteredApplications` y `App Paths`,
sin tocar el valor predeterminado, y notifica al Explorador con
`SHChangeNotify`.
- **`--unregister` borraba claves `.ext` completas** (`RegDeleteTree(".png")`):
destruía la lista de «Abrir con» de todos los programas y la asociación
predeterminada del usuario. Ahora elimina solo los valores propios y, en
instalaciones antiguas, el valor predeterminado únicamente si sigue apuntando
a `ARTPICST.Image`.
- **El filtro del diálogo «Abrir» cubría 23 formatos** (faltaban APNG, DIB, RLE,
PNM/PPM/PGM/PBM, JP2/J2K/JPX, CR3, ORF, RW2, RAF, SR2, KDC, EXIF…): ahora se
genera de la lista única y ofrece los 51.
- **El instalador añade `SupportedTypes`**, de modo que ARTPICST aparece en
«Abrir con» incluso en extensiones que aún no tienen ningún ProgID.

### Integridad real del payload (SHA-256)
- **El instalador verifica el payload que incrusta.** `installer/payload_manifest.hpp`
  se generaba en cada build, pero el instalador nunca lo incluía: definía su
  propia tabla sin hashes y se limitaba a comprobar que los recursos existían.
  La «verificación de integridad» del wizard no verificaba nada. Ahora incluye el
  manifest generado y compara el SHA-256 de los bytes de cada recurso `RCDATA`
  **antes** de extraer nada a disco; una discrepancia aborta la instalación con
  error explícito y `--silent` sale con código 2 (payload incompleto). Si el
  manifest es el placeholder (build de desarrollo sin payload poblado) se avisa
  en la consola de log en lugar de rechazar la instalación.
- **Una sola implementación de SHA-256.** El algoritmo vivía duplicado en el
  actualizador; se ha extraído a `installer/sha256.hpp`, que ahora comparten
  instalador y actualizador (una primitiva de seguridad no debe tener dos copias
  que puedan divergir).
- **CMake genera el manifest** al compilar con `-DARTPICST_PACKAGE=ON`, así que
  el instalador de CMake también verifica (antes compilaba con hashes vacíos).

### Orientaciones EXIF 5 y 7 intercambiadas
- **Las fotos con orientación 5 o 7 se mostraban reflejadas por la diagonal
  equivocada.** El visor compone espejo→rotación (TransformBgra y la matriz de
  visualización) y WIC también voltea antes de rotar (documentado por
  Microsoft), pero el mapa EXIF pedía 90° para la 5 y 270° para la 7: eso da la
  transpuesta invertida en lugar de la transpuesta. Ahora 5 = espejo H + 270° y
  7 = espejo H + 90°, que es lo que describe el propio EXIF.
- **La ruta de respaldo GDI+ se queda como estaba a propósito** (5 =
  `Rotate90FlipX`, 7 = `Rotate270FlipX`): `RotateFlipType` rota antes de
  voltear, así que su tabla equivalente es la simétrica. Queda comentado en el
  código para que nadie «unifique» las dos tablas y vuelva a intercambiarlas.
- La extensión de shell (miniaturas y vista previa) usa la misma corrección:
  `WICBitmapTransformRotate270 | FlipHorizontal` para la 5 y `Rotate90 |
  FlipHorizontal` para la 7.

### Endurecimiento del updater y de la extensión de shell
- **Tamaño del asset verificado** contra el `size` que publica la API, siempre
  (no solo cuando hay `digest`): una descarga truncada o una respuesta de proxy
  que devuelve HTML con HTTP 200 se descarta en vez de ejecutarse.
- **Nombre del asset validado** antes de construir la ruta local: un nombre con
  separadores, unidad o `..` ya no puede escribir fuera de la carpeta de
  descargas.
- **El shell ya no trunca el último bloque.** `IStream::Read` devuelve `S_FALSE`
  cuando entrega menos bytes de los pedidos, y el bucle salía **antes** de
  insertarlos: los archivos leídos por el respaldo `stb_image` (PSD, TGA,
  PNM/PIC) se cortaban justo en su último bloque. Ahora se conservan los bytes y
  después se comprueba `S_FALSE`.

### CI: el instalador ya no lleva un updater falso
- `.github/workflows/build.yml` copiaba `artpicst.exe` con el nombre
  `artpicst_updater.exe` como placeholder (no compilaba el updater). El
  instalador publicado desde ese workflow llevaba un auto-actualizador que en
  realidad era el visor. Ahora compila `updater/artpicst_updater.cpp` con su
  `.rc`, ejecuta `--selftest` como gate y prepara el payload con
  `scripts/collect_app_payload.py` (que genera los hashes).

### Orientación EXIF con XMP antes del EXIF
- El lector de orientación abandonaba el escaneo en el **primer** APP1. Un JPEG
  con el bloque XMP (`http://ns.adobe.com/xap/1.0/`) antes del bloque EXIF se
  mostraba sin orientar. Ahora solo termina si ya encontró EXIF (o si el
  segmento no se pudo leer completo); un APP1 ajeno se salta y el escaneo sigue.

### Miniaturas y vista previa del Explorador (extensión de shell propia)
- **Nuevo `artpicst_shell.dll`**: servidor COM en proceso con
  `IThumbnailProvider` (Iconos grandes, Mosaicos y Contenido) e
  `IPreviewHandler` (panel de vista previa, Alt+P). Decodifica con WIC
  escalando **dentro** del decodificador y usa `stb_image` como respaldo para
  PSD, TGA, PNM/PPM/PGM y PIC, que no tienen códec WIC.
- **Orientación EXIF aplicada explícitamente** (`IWICBitmapFlipRotator` sobre el
  tag 274, antes de convertir a PBGRA): WIC no la aplica sola y las fotos
  verticales de móvil se habrían visto tumbadas en miniaturas y vista previa.
- **Recursos y latencia del camino crítico:** WIC decodifica **directamente del
  `IStream`** del shell, sin copiar el archivo a un vector ni envolverlo en otro
  stream en memoria (antes el pico de cada miniatura duplicaba el tamaño del
  archivo); el búfer completo solo se materializa para el respaldo `stb_image`
  (PSD, TGA, PNM, PIC) y ahí se reserva de una vez con `IStream::Stat`. La vista
  previa se decodifica al **tamaño real del panel** (mínimo 256 px, tope 2048) y
  solo se recodifica si el panel crece más de un 25 %, en lugar de decodificar
  siempre 4096² (64 MB) para un panel de 300 px. Sin compilador en el equipo de
  desarrollo no hay cifras medidas: son reducciones de trabajo y de pico de
  memoria, no una mejora de velocidad medida.
- **Registro sin secuestrar nada**: se escribe siempre en
  `SystemFileAssociations\<ext>\ShellEx\{GUID}` y, a nivel de extensión, solo si
  esa extensión no tenía ya un manejador, de modo que JPG/PNG/HEIC conservan el
  proveedor nativo de Windows y los formatos sin miniatura ganan una.
- **El instalador es quien registra**, mediante los exports
  `ArtpicstRegisterShell`/`ArtpicstUnregisterShell` con HKLM o HKCU según la
  elevación real (nada de `regsvr32`, que siempre escribiría en HKLM). Nueva
  etapa «Activando miniaturas del Explorador» en la instalación (34 s) y
  «Retirando miniaturas del Explorador» en la desinstalación (14 s).
- **Desinstalación y rollback limpios**: se retiran los CLSIDs propios y las
  asociaciones **solo si siguen apuntando a nuestro manejador**; si otro visor
  las reemplazó, se respeta. El payload pasa a 6 recursos `RCDATA` (201..206).
- **Nuevo objetivo de build** en `build.ps1`, CMake, `Makefile`,
  `build_mingw.bat` y los dos workflows, con el mismo baseline SSE4.2.

### Reproducción GIF a ritmo real
- **La reproducción de GIF ya no se ralentiza.** El temporizador adelantaba el
  reloj virtual a `now` en cada tick, así que el retraso habitual de los
  temporizadores de Windows (~15,6 ms) se perdía en cada fotograma y un GIF de
  2 s duraba bastante más, sin poder recuperarse (el cociente nunca llegaba a
  2). Ahora el reloj avanza solo por el tiempo **consumido** por los fotogramas
  mostrados, se saltan fotogramas si el sistema se retrasa de verdad y el
  siguiente aviso se programa por lo que **falta** del fotograma, no por su
  duración completa: sin deriva, con duración exacta y aritmética modular
  segura para el desbordamiento de `GetTickCount` (49,7 días).
- **Compatibilidad alineada con el visor:** el instalador asocia también
  `.icon`, que el visor ya abría pero quedaba fuera de «Abrir con».

### Instalador y desinstalador: preflight y limpieza real
- **La casilla "conservar la configuración" ya hace algo.** Estaba en la UI pero
  ninguna tarea la leía: el desinstalador borraba `Software\ARTPICST` (incluidas
  las preferencias del visor) aunque estuviera marcada. Ahora, al conservar,
  solo se retiran `InstallDir` y `Version`; al desmarcar, se elimina el árbol
  completo y también `%LOCALAPPDATA%\ARTPICST`.
- **Limpieza de la caché de actualizaciones** (`%LOCALAPPDATA%\ARTPICST\updates`)
  como nueva etapa del desinstalador, siempre, conserve o no la configuración.
- **Preflight de espacio en disco** antes de extraer: se exige el payload más
  32 MB de holgura y se registra espacio libre/necesario; antes una unidad casi
  llena fallaba a mitad de la instalación. Si el dato de espacio no está
  disponible, no se bloquea la instalación.
- El desinstalador pasa a 6 etapas (14 s) con la limpieza de datos de usuario
  al final y un mensaje explícito si el visor sigue abierto y retiene el log.

### Ajuste de código para el equipo de desarrollo (Intel Core i5-12400)
- Los pipelines MSVC añaden `/favor:INTEL64` (por defecto MSVC usa `blend`),
  `/Gw` en el visor (datos globales con LTCG) y `/Zc:inline` en los tres
  binarios. Ninguno altera el baseline SSE4.2 ni emite AVX: el gate de política
  SIMD y el de consistencia siguen pasando.
- Medido en el equipo: i5-12400 (SSE4.2/AVX2, sin AVX-512), Radeon RX 580 4 GB,
  16 GB RAM, 1680×1050@60 Hz, Windows 11 26H2 (build 26300). AVX2 queda **sin
  usar a propósito** porque el proyecto prohíbe AVX por diseño (CPUs previas a
  2008 fallarían); habilitarlo exige cambiar la política y su gate.

### Suavidad y recursos en instalador y actualizador
- **Búfer de composición persistente** (`installer/ui_theme.hpp`): los dos
  binarios creaban y destruían un HDC de memoria y un bitmap del tamaño de la
  ventana en cada `WM_PAINT` (hasta 60 veces por segundo). Ahora el búfer se
  reutiliza y solo se recrea al cambiar el tamaño del cliente, con reserva de
  pintado directo si no hay memoria.
- **Animación del progreso medida en tiempo real** (`SmoothTowards`): la
  interpolación ya no depende del número de ticks, así que el recorrido es el
  mismo a 30, 60 o 144 Hz y en equipos lentos, y aterriza exacta en el objetivo
  sin temblor final. El temporizador pasa de 33 a 16 ms con invalidación
  selectiva de las zonas que cambian.
- **Progreso de descarga suavizado** en el actualizador: la barra persigue los
  bytes reales con suavizado temporal y solo se repinta la franja de
  barra+estado, no la ventana completa a 60 fps.
- **Fuentes GDI+ cacheadas** también en el actualizador: se construían cuatro
  `Font` y un `FontFamily` en cada fotograma (el mayor coste del repintado).
- **Correcciones:** la barra del instalador podía quedarse congelada en ~98 % al
  terminar (el temporizador se detenía antes de reflejar el 100 %);
  `PurgeOldDownloads` borraba también las descargas recientes por mezclar epochs
  FILETIME (1601) y Unix (1970); y `WM_NCHITTEST` del actualizador interpretaba
  coordenadas de pantalla como de cliente, lo que podía impedir pulsar los
  botones de la notificación.

### Mejoras Ultra: EXIF fiable, navegación fluida y calidad
- Comparte entre JPEG, WIC y las pruebas un único mapeo constexpr y comprobable
  de las ocho orientaciones EXIF, incluidos los cuatro modos espejo y los
  valores ausentes o inválidos.
- Lee la orientación WIC a través de `IWICMetadataQueryReader` —la interfaz
  correcta de Windows Imaging Component— y comprueba las variantes y el rango
  de cada valor antes de aplicarlo. Incluye rutas habituales para JPEG EXIF,
  TIFF y XMP; los formatos sin metadatos de orientación permanecen sin cambios.
- Valida los metadatos EXIF de GDI+ por tipo, longitud y límites usando
  aritmética comprobada y `memcpy`, evitando lecturas fuera del búfer y accesos
  desalineados ante archivos dañados.
- Amplía la caché LRU a 8 imágenes sin superar su presupuesto estricto de 64 MB
  y prepara la siguiente y la anterior. Si la carpeta o la navegación cambian
  durante una decodificación, descarta el trabajo obsoleto.
- El asignador BGRA, alineado a 64 B, comparte de forma segura imágenes
  inmutables entre visor y caché con referencias atómicas: se eliminan las
  copias de frames completos al navegar y se mantiene protegido el desalojo
  concurrente.
- Mantiene reutilizable el staging GPU pequeño, pero devuelve al sistema el
  staging grande que ya se ha copiado a la textura. Al copiar una imagen
  transformada al portapapeles, solo reserva el buffer temporal adicional cuando
  hay que combinar rotación y volteo.
- Asigna una generación a cada carga de imagen y la incluye en la identidad de
  la caché GPU: volver a abrir la misma ruta nunca reutiliza una textura anterior.
- Durante el arrastre, invalida únicamente la región de imagen modificada en vez
  de volver a pintar toda la ventana en cada movimiento.
- Corrige el validador SIMD: ahora interpreta argumentos `static_cast<int>`
  anidados, sigue detectando activaciones reales de AVX y acepta los nombres
  prohibidos mencionados únicamente en comentarios. Añade pruebas del mapeo
  EXIF completo y actualiza los límites documentados.

### Baseline SIMD SSE4.2 (sin AVX) y cobertura real de los kernels
- **El núcleo de imagen pasa de SSE2 a SSE4.2.** El intercambio B↔R se resuelve
  con un solo `PSHUFB` (antes 2 AND + 2 desplazamientos + 2 OR); la réplica del
  alfa en el premultiplicado, con otro `PSHUFB` (antes 4 operaciones); la
  restitución del alfa original, con `PBLENDVB` (antes 2 AND + OR); y las dos
  detecciones de opacidad, con un único `PTEST` (antes `movemask` + comparación).
  La identidad de redondeo `(c·a + 127)/255` y la LUT de claridad se mantienen
  **bit a bit idénticas**.
- **AVX/AVX2/AVX-512 quedan prohibidos y vigilados.** `src/image_core.hpp` aborta
  en compilación si detecta `__AVX__`/`__AVX2__`/`__AVX512F__`, y el nuevo
  `tests/check_simd_policy.py` falla el build si algún fuente, script o workflow
  contiene un token capaz de activarlos. Los scripts de build y los tres
  workflows fijan `/arch:SSE4.2` (MSVC) o `-msse4.2` (MinGW) con sonda previa de
  toolchain: el build **no** se degrada a SSE2 en silencio.
- **Los kernels vectorizados no estaban cubiertos por los tests.** Los tests
  reservaban con `std::vector`, cuyas direcciones solo están garantizadas a 16 B:
  con `kPixelAlignment = 64 B` la comprobación `IsAligned(px, 64)` era falsa y solo
  se ejecutaba la cola escalar. El nuevo `TestSimdKernelsAligned` usa el asignador
  real, **verifica la alineación** y compara cada kernel contra su referencia
  escalar byte a byte.
- **Rendimiento medible:** nuevo `tests/kernel_bench.exe` (ns/píxel y GiB/s por
  kernel, con comparación SSE2 vs SSE4.2). Metodología en `docs/PERFORMANCE.md`.

### Calidad de origen y detalle de zoom
- **El límite de carga deja de estrangular el detalle.** El tope fijo de 3840 px
  reducía la foto *durante la carga*, así que el zoom al 100 % ya no mostraba los
  píxeles reales. Ahora se deriva de un presupuesto de memoria de 100 MB
  (lado máximo 5000 px, suelo 3840, techo 16384) y se puede ajustar con
  `ARTPICST_MAX_DIMENSION`. El valor efectivo se registra en el log.

### Diagnóstico, errores y validación
- **Logging real.** `LogMessage` pasa a un logger con niveles (INFO/WARN/ERROR),
  marca temporal, identificador de hilo, **rotación** a 1 MB y destino estable en
  `%LOCALAPPDATA%\ARTPICST` (antes `%TEMP%`, sin nivel ni límite). La escritura se
  serializa con un mutex porque el chequeo de actualizaciones corre en otro hilo.
- **Guardián de fallos:** `SetUnhandledExceptionFilter` + `std::set_terminate`
  registran la excepción y su dirección antes de terminar; antes un fallo no
  capturado cerraba la aplicación sin dejar rastro.
- **Todo error queda por escrito:** `HandleError` (punto único de los fallos de
  decodificación, E/S y portapapeles) registra a nivel ERROR; también se registran
  la indisponibilidad de Direct2D/DirectWrite, la pérdida del dispositivo GPU
  (con límite para no inundar el log) y los fallos de `SetTimer`.
- **Comprobación de versión de Windows** con `RtlGetVersion` (build real, no el
  falseado por el manifiesto) antes de usar atributos DWM 19/20/35/36/38.
- **Instalador:** verifica **SSE4.2 en la CPU** y Windows 10 (build 10240) antes
  de tocar el sistema, y comunica el estado de cada dependencia en la consola
  del wizard. Se añade `AppUserModelID` propio por rol (visor / instalador) para
  que la barra de tareas agrupe y muestre el icono correctos.

### Build, empaquetado y documentación
- **Enlace endurecido** en los tres binarios: `/GUARD:CF /DYNAMICBASE
  /HIGHENTROPYVA /NXCOMPAT`. No se usa `/fp:fast` (rompería la bit-exactitud).
- **Nuevo `Makefile`** (Git Bash / MSYS2 / MinGW-w64) con los objetivos `all`,
  `test`, `policy`, `bench`, `package`, `dist`, `msvc` y `clean`.
- **Nuevo `scripts/verify.ps1`**: un único comando que ejecuta todos los gates de
  calidad y sale con código distinto de cero si alguno falla.
- **CMake:** `ARTPICST_PACKAGE` (por defecto OFF) elimina el `FATAL_ERROR` que
  impedía cualquier `cmake --build` en limpio sin payload, y se añaden por fin los
  objetivos de test, el gate de política SIMD y el banco de kernels.
- **Documentación nueva:** `docs/PERFORMANCE.md` (baseline SIMD, kernels,
  metodología de medición, presupuesto de memoria) y `docs/INSTALL.md`
  (instalador, rutas, accesos directos, modos silenciosos, códigos de salida,
  dependencias y desinstalación limpia).

### Zoom ultra suave, libre y optimizado
- **Motor de zoom logarítmico**: la animación interpola `ln(zoom)` (progresión
  geométrica) en lugar del factor lineal: el tempo perceptual es idéntico a
  cualquier escala (0.5%→1% se siente como 100%→200%) y encadenar muescas de
  rueda compone sobre el objetivo en curso sin acelerones ni "muelles". El
  pivote queda clavado bajo el cursor en cada paso, con reanclaje sin deriva
  a mitad de animación. Duración adaptativa 90–140 ms según el salto.
- **Zoom libre sin centrado ni límites prácticos**: se eliminó el clamp y el
  recentrado automáticos (`EnsureImageVisible` ya no encierra la imagen; si
  queda fuera de la ventana, `F` / doble clic / botón "Ajustar" la recuperan)
  y el auto-snap a 100% durante la rueda (el 1:1 exacto sigue en tecla `1`/`0`,
  botón "1:1" y menú). Rango ampliado de 0.01x–200x a **0.005x–512x**.
- **Región sucia del zoom (GPU y GDI+)**: cada paso de la animación invalida
  solo la unión del rectángulo de pantalla que ocupa la imagen antes y después
  de moverse (+2 px de margen), no la ventana completa ~120 veces/s; el `BitBlt`
  final es parcial en la ruta GDI+ y Direct2D rasteriza solo la franja de
  movimiento. Con fallback seguro a repintado completo.
- **Caché de recursos del dock/OSD (ruta GDI+)**: los ~15 objetos GDI+ que se
  creaban y destruían por frame (paths del dock/sombra/11 botones, pinceles,
  penes, degradado activo) y la medición del texto del OSD ahora se cachean y
  solo se recrean al cambiar tema, geometría o mensaje. Mismo resultado visual.
- **Caché de layout del dock**: `LayoutHud` solo recalcula cuando cambia el
  tamaño del cliente, el modo pantalla completa o la etiqueta dinámica; antes
  se reconstruía en cada frame y en cada movimiento de ratón (vía `HitTestHud`).
- **Caché de `ImageAttributes`**: la matriz de color de efectos (grises /
  negativo / claridad) se construye una vez por efecto activo, no en cada frame.
- El zoom por teclado (↑/↓, +/−) y menú pivota sobre el **centro de la ventana**
  (punto fijo de referencia, no un recentrado).
- Corrección de compilación (MSVC 18, error C2660): `new (std::nothrow)` no
  compila sobre tipos GDI+ porque `GdiplusBase::operator new` no expone la
  forma alineada; el `ImageAttributes` cacheado se construye ahora con `::new`
  de colocación sobre almacenamiento `alignas` propio y se destruye con llamada
  explícita al destructor (mismo orden seguro respecto a `GdiplusShutdown`).

## [1.2.1] — 2026-09-27

Pase de auditoría completa: corrección de 7 defectos de seguridad/corrección,
tests y fuzzing automatizados, warnings estrictos y gates de CI.

### Seguridad (críticos)
- **EXIF**: lectura fuera de límites con JPEGs manipulados — el offset del IFD
  se comparaba en `uint32_t` y un valor cercano a `0xFFFFFFFF` desbordaba y
  pasaba el chequeo. Ahora se compara en 64 bits y el valor leído se sanea al
  rango 1..8.
- **Updater**: la verificación SHA-256 era *fail-open* — si el hash no podía
  calcularse se consideraba superada y se ejecutaba el instalador sin
  verificar. Ahora es *fail-closed*: hash no calculable ⇒ descarte.
- **Parser JSON**: escape `\u` desreferenciaba más allá del fin de cadena con
  respuestas truncadas; y una cadena sin cierre provocaba bucle infinito.

### Corrección
- `DecodeWithWic` liberaba un buffer de `AlignedPixelAlloc` con `free()`
  (corrupción de heap) en la ruta de error.
- La exportación guardaba al **tamaño de pantalla** (una foto 6000×4000 se
  convertía en 1920×1080); ahora exporta a la resolución de la imagen
  transformada y valida el códec antes de guardar.
- Updater `--background` dormía 1,5 s y mataba la descarga a medias; ahora
  espera al hilo con timeout de 10 minutos.
- El guard de 60 s sin datos nunca disparaba (se actualizaba la marca antes
  de comparar).
- `SetAsWallpaper` fallaba en silencio con formatos no soportados por Windows.
- Render GPU con DPI fijo 96 pese al manifiesto PerMonitorV2 (texto/OSD
  borrosos al 125/150 %); ahora usa el DPI real del monitor y recrea el target
  al arrastrar entre monitores.
- Instalador desatendido: `--silent`/`--dir` con exit codes documentados,
  validación del payload antes de tocar el sistema y desinstalación silenciosa
  que resuelve la carpeta registrada.

### Añadido
- Menú "Buscar actualizaciones" con atajo **Ctrl+U**; chequeo diario que
  respeta el checkbox de auto-instalación del updater.
- Módulos testeables: `installer/release_json.hpp` (parser compartido) y
  `src/jpeg_exif.hpp` (EXIF).
- Suite de tests: núcleo de imagen (9 suites + fuzz de allocator), EXIF
  (JPEGs sintéticos válidos/hostiles) y fuzzer JSON determinista (mutaciones,
  truncamientos exhaustivos, basura binaria).
- CI: gate de versión (version.json ↔ version.hpp ↔ recursos), tests en cada
  push y **fuzzing nocturno con AddressSanitizer**.
- Warnings estrictos en los tres binarios: `/W4 /permissive-` (MSVC) y
  `-Wall -Wextra` (MinGW).

### Modificado
- Menú contextual: estados activos con ✓ (Ultra-Claridad, B/N, Negativo,
  presentación, pantalla completa) en lugar de reescribir etiquetas.
- Diálogos con medición GDI+ real (sin recortes en textos largos) y tamaño
  limitado al área de trabajo.
- "Acerca de": C++23 + Direct2D, repo y licencia centralizados.
- Guía F1 sincronizada con los atajos reales (F1, Esc, Ctrl+Shift+A, Ctrl+U).

### Eliminado
- Código muerto: `CalculateOptimalTextSize`/`TextSizeInfo` (~90 líneas) y
  variables sin uso.

## [1.2.0] — 2026-08-30

Versión base del visor con instalador autocontenido y auto-actualizador.
