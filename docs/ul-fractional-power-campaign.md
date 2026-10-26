# Campaña de potencia de uplink fraccional

Comparación pareada del caudal de uplink entre `ul_power_mode: fixed` y `ul_power_mode: fractional` en los siete perfiles offline del repositorio. Los escenarios que se entregan usan modo fraccional. Esta campaña se hizo sobre copias temporales, cuando esos escenarios todavía estaban en fijo. Las baselines históricas llevan `ul_power_mode: fixed`, el nombre nuevo de `set_ul_pow: true`, y siguen ejecutándose en fijo.

## Qué se comparó

Cada perfil se ejecutó diez veces en modo fijo y otras diez en modo fraccional. Las semillas son 20260927, 20260928, …, 20260936. La primera es la semilla del protocolo de evidencia. La semilla de una pasada fija es la misma que la de su pasada fraccional, y las diez semillas son distintas entre sí. El perfil de control trae `seed: 24` en el fichero; la copia de la campaña lo sustituye.

La duración es la del perfil, 180 s, con `period: -1`. El binario es `./bin/fikore` sobre la copia. No se ha usado `run_offline.sh`, porque ese script pinta figuras y reescribe el directorio de resultados más reciente.

En modo fijo el UE transmite `tx_power_ul` repartido entre los PRB concedidos. En modo fraccional la potencia total pedida es P0 + α·PL + 10·log10(M), y el único recorte es el mínimo contra `tx_power_ul`. No hay suelo. M vacío se cuenta como un PRB, igual que antes. P0 es `nominal_pusch_p0` y α es `alpha_ul` (0,8 en estos perfiles). La pérdida de la fórmula es la estimación LOS de control, no el mapa completo.

En estos perfiles `tx_power_ul` vale 23 dBm, o 10 dBm en el fondo del indoor y de los dos n258. Ese valor es el techo nuevo. El modo fijo de la campaña coincide con el comportamiento que ya se entregaba: la potencia fija es 10 o 23 dBm, y el recorte antiguo a [10, 23] no la movía. El suelo antiguo solo podía subir una petición fraccional por debajo de 10 dBm. Lo que cambia de un brazo al otro es la fórmula, no un suelo aplicado al brazo fijo.

El scheduler no hace un segundo pase después de repartir la potencia. El MCS se recalcula con la potencia ya repartida, pero la concesión no se encoge.

## Por qué una pareja de semillas debería compartir la trayectoria

La movilidad y el tráfico aleatorios salen del generador inicializado con `seed`. Los dos brazos recorren ese generador en el mismo orden: el modo de potencia no pide números aleatorios nuevos antes de mover al UE. Con la misma semilla, la trayectoria y la oferta de tráfico deberían coincidir, y el caudal solo debería moverse por la potencia y por lo que esa potencia hace en el MCS y en la interferencia.

Eso es una expectativa, no una prueba. Por eso cada pareja compara la traza de movilidad de cada UE, no solo la distancia impresa al final. La copia deja `log_ue` y `log_mobility` activos y apaga el resto de logs de fichero. La traza se escribe una vez por segundo, de t = 0 a t = 180 (181 muestras). La distancia es la del UE al origen, que es donde está la estación. Si el máximo de |Δd| entre las dos trazas pasa de 1 m, esa pareja no se usaría para atribuir el caudal a la potencia.

## Cómo se lee el caudal

La cifra es la línea final `UL cons. rate`: Mbit/s entregados en los 180 s, sin quitar un calentamiento. El UE de estudio es el primero del `.ini` (identificador numérico 0, un solo UE). El fondo son los demás. Para cada semilla el fondo se resume como la media de esos UE; la media, el mínimo y el máximo de después son los de esas diez cifras, no los de cada UE suelto.

El porcentaje es el de las medias: (media fraccional − media fija) / media fija. Cuando una semilla tiene un caudal fijo casi nulo, el porcentaje de esa semilla se dispara aunque el cambio en Mbit/s sea despreciable. En esos casos se dice explícitamente. Un cambio de menos de 0,05 Mbit/s en una semilla se cuenta como plano.

| Perfil | Clase | Techo | Fijo, media [mín, máx] | Fraccional, media [mín, máx] | Δ pareado, media [mín, máx] | % de las medias | Semillas |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| UMi n40 | estudio | 23 | 11,76 [7,38, 14,74] | 11,87 [7,43, 14,81] | +0,11 [−0,20, +0,24] | +1,0 % | 8 suben, 2 bajan |
| UMi n40 | fondo (10) | 23 | 1,11 [0,91, 1,32] | 1,19 [0,94, 1,42] | +0,09 [+0,03, +0,18] | +7,8 % | 9 suben, 1 plana |
| UMa n78 | estudio | 23 | 6,68 [3,80, 8,17] | 6,30 [3,44, 8,10] | −0,38 [−0,64, −0,07] | −5,6 % | 10 bajan |
| UMa n78 | fondo (20) | 23 | 0,78 [0,36, 1,09] | 0,79 [0,37, 1,09] | +0,01 [0,00, +0,03] | +1,2 % | 10 planas |
| Rural n78 | estudio | 23 | 10,16 [1,13, 18,07] | 10,16 [1,13, 18,07] | 0,00 [−0,00, +0,01] | 0,0 % | 10 planas |
| Rural n78 | fondo (10) | 23 | 2,66 [0,89, 3,89] | 2,66 [0,89, 3,89] | 0,00 | 0,0 % | 10 planas |
| Indoor n78 | estudio | 23 | 19,35 [3,58, 29,23] | 20,24 [5,15, 29,23] | +0,89 [−0,02, +1,57] | +4,6 % | 9 suben, 1 plana |
| Indoor n78 | fondo (10) | 10 | 4,19 [2,67, 5,27] | 4,20 [2,54, 5,27] | +0,01 [−0,18, +0,60] | +0,3 % | 2 suben, 5 bajan, 3 planas |
| n258 FWA | estudio | 23 | 100,00 [99,97, 100,02] | 100,00 [99,97, 100,02] | 0,00 | 0,0 % | 10 planas |
| n258 FWA | fondo (10) | 10 | 5,72 [5,65, 5,86] | 5,72 [5,65, 5,86] | 0,00 | 0,0 % | 10 planas |
| n258 alta pérdida | estudio | 23 | 14,29 [0,02, 29,22] | 17,36 [0,07, 35,20] | +3,07 [+0,05, +7,55] | +21 % | 10 suben |
| n258 alta pérdida | fondo (10) | 10 | 2,63 [1,19, 6,35] | 3,16 [1,20, 6,40] | +0,54 [+0,00, +1,25] | +20 % | 9 suben, 1 plana |
| Indoor control | estudio | 23 | 12,27 [2,47, 25,46] | 12,41 [2,66, 24,94] | +0,14 [−0,52, +0,81] | +1,1 % | 6 suben, 4 bajan |
| Indoor control | fondo (5) | 10 | 3,09 [2,18, 3,73] | 3,17 [2,30, 3,73] | +0,09 [−0,03, +0,21] | +2,8 % | 7 suben, 3 planas |

Las unidades de la tabla son Mbit/s. El fondo entre paréntesis es el número de UE de esa clase. P0 del estudio es −60 dBm en todos. P0 del fondo es −50 dBm, salvo en el rural, donde también es −60 dBm.

## Perfil a perfil

**UMi n40, 20 MHz a 2,38 GHz.** El estudio pide 15 Mbit/s y entrega unos 12. El fondo pide 2 y entrega cerca de 1,1. En el estudio el salto medio es una décima de Mbit/s y el signo no se sostiene: dos semillas bajan unas dos décimas. No es un cambio estable. En el fondo la subida es pequeña en Mbit/s (+0,09) y más visible en porcentaje (+7,8 %) porque la base es baja. Nueve semillas suben y la décima sube +0,03, justo por debajo del umbral de 0,05. La dirección del fondo sí es estable; el tamaño no pasa de dos décimas.

**UMa n78 peatonal, 100 MHz a 3,5 GHz.** El estudio pide 30 Mbit/s y se queda en unos 6,7, así que el límite es la radio, no el generador. Las diez semillas bajan, entre −0,07 y −0,64 Mbit/s. La media es −0,38 Mbit/s, un −5,6 %. Es un cambio pequeño y estable en el signo. El fondo, veinte UE a 2 Mbit/s pedidos, no se mueve: las diez semillas quedan por debajo de 0,03 Mbit/s. La lectura que encaja es que el estudio, con P0 = −60, transmite por debajo de 23 dBm cuando la concesión es moderada, y esa potencia de menos le baja el MCS; el fondo, con P0 = −50, ya iba contra el techo de 23 dBm y por eso no cambia. Es una lectura del resultado, no una traza de potencia por PRB.

**Rural n78 vehicular, 100 MHz a 3,5 GHz.** Estudio y fondo tienen el mismo P0 (−60) y el mismo techo (23 dBm). El estudio pide 20 Mbit/s y la media queda en 10,2, con un abanico enorme entre semillas (de 1,1 a 18,1). Ese abanico es el mismo en los dos modos. El delta pareado cabe en ±0,005 Mbit/s en el estudio y en ±0,001 en el fondo. Aquí el modo fraccional no hace nada visible: con la pérdida rural la petición P0 + α·PL llega al techo en las concesiones que importan, y a partir de ahí la fórmula coincide con el modo fijo.

**Indoor n78, 100 MHz a 3,5 GHz.** El estudio pide 30 Mbit/s, tiene techo 23 dBm y sube de 19,35 a 20,24 de media (+0,89 Mbit/s, +4,6 %). Nueve semillas suben, hasta +1,57. Una no se mueve (−0,02). La semilla que parte de 3,58 llega a 5,15: es un +44 % relativo y por eso la media de los porcentajes semilla a semilla (+9 %) es mayor que el +4,6 % de las medias. La dirección es casi estable; el tamaño depende de la semilla. El fondo tiene techo 10 dBm y pide 6 Mbit/s. Su media no se mueve (+0,01) y el signo cambia de semilla a semilla, con una que sube +0,60 y otras que bajan hasta −0,18. No hay un efecto de fondo que se pueda contar como sistemático.

**UMi n258 FWA, 400 MHz a 26 GHz, estático.** El estudio pide 100 Mbit/s y los entrega en las diez semillas, en los dos modos (99,97 a 100,02). El caudal lo marca el generador. El fondo pide 10, tiene techo 10 dBm y se queda en 5,72 en ambos brazos, con deltas por debajo de la milésima. No aparece efecto del modo.

**UMi n258 de alta pérdida, misma portadora y la misma oferta.** El estudio ya no llega al objetivo: 14,3 Mbit/s en fijo y 17,4 en fraccional. Las diez semillas suben. El delta medio es +3,07 Mbit/s (+21 % sobre las medias), pero el rango va de +0,05 a +7,55. La semilla 20260934 pasa de 0,020 a 0,073 Mbit/s. Ese +270 % no es un cambio de régimen: los dos brazos siguen sin caudal. En el otro extremo, una semilla que ya iba en 29,2 pasa a 29,8, y otra pasa de 27,6 a 35,2. La dirección es estable y el tamaño no. El fondo, con techo 10 dBm, pasa de 2,63 a 3,16 (+0,54 Mbit/s, +20 %). Nueve semillas suben y una queda en +0,005. También aquí el signo se sostiene y la magnitud va de casi nada a +1,25 Mbit/s.

**Indoor de control.** Es el indoor anterior con cinco UE de fondo en lugar de diez. El estudio pide los mismos 30 Mbit/s y queda más bajo (unos 12 Mbit/s). El delta medio es +0,14 Mbit/s (+1,1 %) y el signo no se sostiene: seis semillas suben y cuatro bajan, entre −0,52 y +0,81. No es un efecto estable. El fondo sube +0,09 Mbit/s (+2,8 %). Siete semillas suben, tres quedan planas y ninguna baja más de 0,03. Es una subida leve y, en el signo, bastante repetida.

## Distancia

Las setenta parejas tienen la traza completa de cada UE. El número de trazas coincide con el de UE del perfil: 11, 21, 11, 11, 11, 11 y 6. En todas, la diferencia absoluta máxima es 0 m y la media de esa diferencia es 0 m. No hay ninguna pareja marcada. El caudal de arriba no se debe a que un brazo haya caminado o conducido por otro sitio.

## Lectura y reservas

El modo fraccional no es un cambio uniforme. Donde la petición abierta ya choca con `tx_power_ul`, el caudal no se mueve: el rural en las dos clases y el n258 FWA de fondo. Donde el generador ya entrega el objetivo, tampoco se ve: el estudio del n258 FWA. El único sitio con una subida clara y del mismo signo en las diez semillas es el n258 de alta pérdida, estudio y fondo, y aun ahí el tamaño depende mucho de la semilla. El indoor de diez UE de fondo sube el estudio de forma casi sistemática, cerca de 1 Mbit/s, y deja el fondo quieto. El UMa hace lo contrario en el estudio, una bajada pequeña en las diez semillas, y no toca el fondo. En n40 y en el indoor de control el estudio cambia de signo entre semillas; el fondo de esos dos perfiles sube poco y casi siempre.

La cifra es el caudal de los 180 s enteros, sin descartar el arranque. No hay lazo cerrado de comandos de potencia. El techo es el `tx_power_ul` de cada clase, no un 23 dBm aparte. La concesión no se vuelve a calcular después de repartir la potencia, así que un UE que recibe muchos PRB puede seguir quedándose corto de potencia por PRB cuando ya está en el techo. Ajustar el scheduler para eso queda fuera de esta campaña.
