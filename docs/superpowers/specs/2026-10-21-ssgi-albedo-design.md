# SSGI per OpenMW: render target albedo e mod OMWFX

Data: 2026-10-21. Primo sottoprogetto di una suite grafica ispirata a Skyrim Community Shaders.

## Obiettivo

Aggiungere illuminazione indiretta in spazio schermo (SSGI) e ambient occlusion a OpenMW.
L'effetto deve cambiare l'aspetto dei contenuti vanilla senza texture nuove.

Il lavoro ha due prodotti:

1. Una merge request C++ per OpenMW upstream, "Albedo RT", che espone il colore diffuso delle superfici alle tecniche OMWFX.
2. Una mod distribuibile, "omw-ssgi", composta da file `.omwfx`.

Skylighting, PBR, wetness e le altre funzioni di Community Shaders restano fuori. Ognuna avrà la sua spec.

## Contesto

- Il renderer è OSG. I branch upstream `vsg/*` preparano il passaggio a VulkanSceneGraph. Per questo la parte C++ resta piccola e la logica dell'effetto vive in OMWFX.
- OMWFX espone già colore, depth, normali (`pass_normals`), luci (`pass_lights`), `omw.prevViewMatrix` e `omw.projectionMatrix`.
- I `render_target` OMWFX mantengono il contenuto tra un frame e l'altro (`docs/source/reference/postprocessing/omwfx.rst:611`). Servono come history buffer per l'accumulo temporale.
- Manca l'albedo. Senza albedo la GI applicata al colore finale scurisce anche emissive, speculari e luci dirette, e il rimbalzo indiretto non prende il colore della superficie.

## Parte 1: merge request "Albedo RT"

### Interfaccia OMWFX

- Nuova chiave nel blocco `technique`: `pass_albedo = true;`.
  Viene parsata in `components/fx/technique.cpp` accanto a `pass_normals` e vale `false` quando il backend non la supporta.
- Nuovo metodo `Fx::Technique::getAlbedo()`, analogo a `getNormals()` (`components/fx/technique.hpp:151`).
- Nuova macro `OMW_ALBEDO` (`0` o `1`) in `components/fx/pass.cpp`, accanto a `OMW_NORMALS`.
- Nuovo sampler `omw_SamplerAlbedo` e nuova funzione `vec3 omw_GetAlbedo(vec2 uv)`, con il ramo multiview `sampler2DArray` come `omw_GetNormals`.
- Documentazione aggiornata in `omwfx.rst`: tabella dei sampler, tabella delle macro, tabella delle funzioni, chiavi del blocco `technique`.

### Lato motore

- `PostProcessor::TextureIndex` riceve `Tex_Albedo`. `TextureArray` passa da 7 a 8 elementi.
- `PostProcessor::TextureUnits` riceve `Unit_Albedo` prima di `Unit_NextFree`.
- `mAlbedo` e `mPrevAlbedo` seguono lo stesso ciclo di `mNormals` e `mPrevNormals`: si attivano se almeno una tecnica attiva chiede `pass_albedo`, e il cambio ricompila gli shader della scena.
- `postprocessor.cpp:442` imposta anche `defines["disableAlbedo"]`. `shadermanager.cpp:871` riceve il default `{ "disableAlbedo", "1" }`.
- `FBO_Primary` riceve `Tex_Albedo` in `COLOR_BUFFER2` con `Stereo::createMultiviewCompatibleAttachment`.
- Con MSAA, `FBO_Multisample`, `FBO_FirstPerson` e `FBO_Intercept` ricevono un renderbuffer albedo in `COLOR_BUFFER2`, come il renderbuffer delle normali (`postprocessor.cpp:538-562`).
- `PingPongCanvas` riceve `setTextureAlbedo()` e lega la texture su `Unit_Albedo`, come `setTextureNormals()`.
- Il supporto dipende dallo stesso controllo `glDisablei` delle normali (`postprocessor.cpp:214`). Il messaggio di errore cita anche l'albedo.
- Formato: `GL_RGBA8`, filtro nearest, stessa risoluzione della scena.

### Shader della scena

Il valore scritto in `gl_FragData[2]` è il colore diffuso prima dell'illuminazione:
la texture diffusa per `diffuseColor.rgb`, dopo detail map, dark map e decal.

| Shader | Punto di scrittura | Condizione |
|---|---|---|
| `compatibility/objects.frag` | dopo la decal, prima di `lighting` (riga 185 circa) | `#if !defined(FORCE_OPAQUE) && !@disableAlbedo` |
| `compatibility/terrain.frag` | prima di `lighting`, alfa uguale a `gl_FragData[0].a` | `#if !@disableAlbedo` |
| `compatibility/groundcover.frag` | dopo il campionamento diffuso | `#if !@disableAlbedo` |
| `compatibility/bs/default.frag` | dopo il campionamento diffuso | `#if !defined(FORCE_OPAQUE) && !@disableAlbedo` |
| `compatibility/bs/nolighting.frag` | scrive `vec4(0.0)` | `#if !defined(FORCE_OPAQUE) && !@disableAlbedo` |
| `compatibility/water.frag` | scrive `vec4(0.0)` | `#if !@disableAlbedo` |

Il terreno disegna un pass per ogni layer con blending additivo, come per le normali (`terrain.frag:110`).
Per questo l'alfa dell'albedo segue l'alfa del colore e non porta altre informazioni.
Un'albedo RGB nulla segna i pixel che non ricevono GI: acqua, superfici senza illuminazione e cielo, che resta al valore di clear `vec4(0.0)`.

### Oggetti trasparenti

`shadervisitor.cpp:621` disattiva la scrittura delle normali sui trasparenti con `osg::ColorMaski(1, false, false, false, false)`.
La MR aggiunge `osg::ColorMaski(2, false, false, false, false)` nello stesso punto e imposta `defineMap["disableAlbedo"] = "1"`.
Vetro, particelle e foglie con blending lasciano così intatta l'albedo degli oggetti dietro.

### Costo

Quando nessuna tecnica chiede `pass_albedo`, l'attachment non viene creato e la define `disableAlbedo` elimina la scrittura dagli shader.
Il costo atteso è zero.

## Parte 2: mod "omw-ssgi"

### File

- `shaders/omw_ssgi.omwfx`: tecnica completa. Richiede la MR, perché il parser di OpenMW non patchato rifiuta la chiave sconosciuta `pass_albedo` con l'errore `unexpected key` (`technique.cpp:266`).
- `shaders/omw_ssao.omwfx`: tecnica di riserva per OpenMW non patchato. Usa solo depth e normali e calcola solo l'AO.
- `shaders/debug_albedo.omwfx`: mostra a schermo il buffer albedo, per i test.

Dentro `omw_ssgi.omwfx` la macro `OMW_ALBEDO` copre l'hardware senza `glDisablei`: con `OMW_ALBEDO == 0` la tecnica ricade sul percorso AO.

### Pass

1. `trace` (mezza risoluzione): per ogni pixel, 4 direzioni per frame con rotazione da rumore blu e `omw.frameNumber`.
   Il ray march su `omw_GetLinearDepth` calcola l'AO a orizzonte (GTAO) e raccoglie l'irradianza da `omw_GetLastShader` nei punti colpiti.
   Output RGBA16F: irradianza in RGB, AO in A.
2. `reproject`: ricava la posizione nel frame precedente da `omw_GetWorldPosFromUV`, `omw.prevViewMatrix` e `omw.projectionMatrix`.
   Mescola con il render target `RT_History` (peso storico 0.9) e scarta la history quando la depth riproiettata differisce oltre una soglia.
3. `store`: copia il risultato in `RT_History` per il frame successivo.
4. `denoise`: filtro bilaterale 5x5 guidato da depth e normali, a mezza risoluzione.
5. `composite` (risoluzione piena): upsampling guidato dalla depth, poi
   `colore = scena - albedo * ambient * (1 - AO) + albedo * irradianza * intensità`,
   dove `ambient` è `omw.ambientColor.rgb`.
   Il contributo si moltiplica per `1 - omw_EstimateFogCoverageFromUV(uv)` e vale zero dove l'albedo RGB è nulla.

### Reset della history

Un render target 1x1 `RT_PrevEye` conserva `omw.eyePos` del frame precedente.
Se lo spostamento della camera supera 500 unità in un frame (teletrasporto, cambio cella), il pass `reproject` ignora la history.

### Parametri utente

Esposti come `uniform_float` e `uniform_bool` nel menu post-processing di OpenMW:
intensità GI (default 1.0), intensità AO (default 1.0), raggio in unità di gioco (default 256), numero di passi del ray march (default 8), filtro temporale acceso o spento.

## Test e criteri di accettazione

### Automatici (nel container)

- `apps/components_tests/fx/technique.cpp`: un test verifica che `pass_albedo = true` venga parsato e che `getAlbedo()` valga `true` con il supporto attivo.
- Un secondo test verifica che `getAlbedo()` valga `false` quando il backend non supporta l'albedo.
- `openmw`, `openmw_tests` e `components_tests` compilano. I test esistenti passano.
- Il diff rispetta `clang-format`.

### Manuali (sul PC dell'utente, con GPU)

Il container non ha GPU né display.

- Tre scene fisse: Seyda Neen di giorno, l'interno della Census and Excise Office, Balmora di notte. Per ognuna uno screenshot con `debug_albedo.omwfx` e uno con `omw_ssgi.omwfx`.
- L'albedo non contiene illuminazione, ombre o nebbia. Vetro e particelle non compaiono nel buffer.
- Il buffer resta corretto con MSAA 4x e in prima persona.
- FPS uguali, entro il rumore di misura, tra OpenMW senza MR e OpenMW con MR senza tecniche albedo attive.
- `omw_ssgi.omwfx` costa al massimo 2 ms a 1080p su una GPU di fascia media, misurati come differenza di tempo GPU nella finestra statistiche di OpenMW (F3) con la tecnica accesa e spenta.
- `omw_ssao.omwfx` funziona su OpenMW non patchato senza errori nel log.

## Ordine di consegna

1. MR "Albedo RT" con test e documentazione. Si propone upstream da sola, perché è utile anche ad altri autori di shader.
2. `debug_albedo.omwfx`, per verificare la MR.
3. `omw_ssao.omwfx`, distribuibile subito su OpenMW non patchato.
4. `omw_ssgi.omwfx`.

## Rischi

- Upstream può rifiutare la MR o chiedere un design diverso, per esempio un G-buffer generico. In quel caso la mod `omw_ssao.omwfx` resta utilizzabile e la MR si adatta alla richiesta.
- La migrazione a VulkanSceneGraph può riscrivere `PostProcessor`. La MR è piccola, quindi il porting costa poco.
- La GI in spazio schermo non vede ciò che è fuori dallo schermo. Ai bordi l'irradianza cala e il filtro temporale riduce lo sfarfallio. Skylighting, nel sottoprogetto successivo, copre questa lacuna per gli esterni.
