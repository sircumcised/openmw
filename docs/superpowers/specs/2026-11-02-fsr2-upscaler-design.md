# Upscaler FSR2 per il fork NerevarMoon (OpenMW 0.51, OpenGL)

Data: 02/11/2026. Ramo: `nm-upscaler`, da `nm-vfxoffset` (70f52540).

## Scopo

Il fork renderizza la scena a una frazione della risoluzione e FSR2 la riporta a 1080p.
Lo scopo è doppio: più fps quando la GPU limita il frame, e un antialiasing temporale che sostituisce SMAA.
openmw-rtx usa FSR 3.1.4 solo su Vulkan col ray tracer (`glsupport.cpp:13`), quindi non si può riusare.

## Scelte già prese

- Upscaler: FSR 2.2 tramite il port OpenGL di Juan Diego Montoya (github.com/JuanDiegoMontoya/FidelityFX-FSR2, licenza MIT).
- Ripiego, se il port non regge dentro OSG: un TAAU nostro in GLSL che riusa gli stessi input del motore.
- Scartati: DLSS (supporto NGX su OpenGL non confermato), FSR 1 spaziale (niente antialiasing temporale).

## Vincoli dal codice

- Il port compila gli shader in SPIR-V e chiama `glSpecializeShader`: servono OpenGL 4.6, `GL_ARB_gl_spirv` e `GL_KHR_shader_subgroup`. La RTX 4070 Laptop li ha.
- Il port usa DSA (`glCreateTextures`, `glTextureStorage2D`) e non ripristina lo stato GL.
- Il port riceve le funzioni GL da un `getProcAddress` passato dal chiamante.
- Il port chiama `glad_glTextureSubImage1D/2D/3D`, ma la sua tabella dichiara quei nomi senza prefisso: serve una patch locale.
- La scena va in `Tex_Scene` (RGBA8), `Tex_Depth`, `Tex_Normal` dentro `FBO_Primary` (`postprocessor.cpp:486`).
- `PostProcessor::renderWidth()` e `renderHeight()` oggi valgono la risoluzione della finestra.
- La prima persona disegna in `FBO_FirstPerson` con un renderbuffer di profondità proprio (`postprocessor.cpp:495`, `npcanimation.cpp:339`).
- Reverse Z è attivo quando `SceneUtil::AutoDepth::isReversed()` è vero.
- Gli shader FX ricevono già `PrevViewMatrix` (`components/fx/stateupdater.hpp:29`).
- Gli scheletrati usano skinning su CPU in `SceneUtil::RigGeometry`, con due geometrie interne alternate per frame.

## Fasi

1. Render scale. Scena e sue texture a `W·s × H·s`; HUD e catena omwfx a `W × H`.
2. Jitter. Sequenza Halton (2,3) dalle funzioni del port (`ffxFsr2GetJitterPhaseCount`, `ffxFsr2GetJitterOffset`), circa 18 campioni a scala 0.67, sommata alla proiezione del mondo e della prima persona.
3. FSR2 con vettori della sola camera e reactive mask per la prima persona. Fine del primo traguardo giocabile.
4. Vettori per oggetto. Matrice modello del frame prima per i drawable rigidi; geometria del frame prima di `RigGeometry` per gli scheletrati.

Gli NPC in movimento sfumano un po' fino alla fase 4.

## Flusso per frame (fasi 1 a 3)

1. La scena disegna in `FBO_Primary` a `W·s × H·s` con la proiezione jitterata.
2. La prima persona disegna con la sua profondità, che con l'upscaler acceso diventa una texture `Tex_FirstPersonDepth`.
3. Un passaggio a schermo intero scrive `Tex_Reactive` (R8): 1 dove `Tex_FirstPersonDepth` differisce dal valore di clear, 0 altrove.
4. Un passaggio a schermo intero scrive `Tex_Velocity` (RG16F). Ogni pixel ricostruisce la posizione da `Tex_Depth` con la proiezione senza jitter e la riproietta con `PrevViewMatrix`.
5. All'inizio di `PingPongCanvas::drawImplementation` parte `ffxFsr2ContextDispatch`. Ingressi: `Tex_Scene`, `Tex_Depth`, `Tex_Velocity`, `Tex_Reactive`, offset del jitter, `frameTimeDelta`, nitidezza. Uscita: `Tex_Upscaled`, RGBA16F a `W × H`.
6. Dopo il dispatch si sporca lo stato OSG: programma, texture unit 0..15, sampler, image unit, UBO.
7. La catena omwfx legge `Tex_Upscaled` come scena. Profondità e normali restano a `W·s × H·s` e si campionano per UV.
8. L'HUD disegna a `W × H`.

Flag FSR2: `FFX_FSR2_ENABLE_DEPTH_INVERTED` se reverse Z è attivo, `FFX_FSR2_ENABLE_DEPTH_INFINITE` se la proiezione ha far infinito. Niente `FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE`: la scena è LDR. Esposizione: texture 1×1 R32F a 1.0.

## Impostazioni (`settings.cfg`, sezione `[Post Processing]`)

| Chiave | Valori | Default | Effetto |
|---|---|---|---|
| `upscaler` | `off`, `fsr2` | `off` | Accende il percorso descritto sopra |
| `upscale scale` | 0.5 .. 1.0 | 0.67 | Frazione della risoluzione per la scena |
| `upscale sharpness` | 0.0 .. 1.0 | 0.3 | Nitidezza RCAS interna a FSR2 |
| `upscale mip bias` | `auto` o numero | `auto` | `auto` vale `log2(s) - 1`, applicato con `osg::TexEnvFilter` sullo stateset radice |

Con `upscaler = fsr2`, MSAA viene forzato a 0 perché FSR2 vuole un ingresso senza multisample. SMAA va tolto dalla catena omwfx a mano.

## Fase 4 in dettaglio

- Drawable rigidi in movimento (porte, attori non scheletrati, proiettili): un cull callback salva la matrice modello del frame prima in una uniform `nmPrevModelMatrix`. Gli shader oggetto scrivono la velocità in un terzo color attachment di `FBO_Primary`.
- Scheletrati: `RigGeometry` ha già la geometria del frame prima nel buffer alternato. Quel vertex array entra come attributo `nmPrevPosition`.
- Il passaggio velocità della fase 3 resta per i pixel senza velocità propria (cielo, terreno, statici).

## Errori e ripiego

- All'avvio si controllano GL 4.6, `GL_ARB_gl_spirv`, `GL_KHR_shader_subgroup` e il risultato di `ffxFsr2ContextCreate`. Un fallimento scrive `[upscaler] fsr2 unavailable: <motivo>` nel log e lascia il gioco a scala 1.0 senza upscaler.
- `PostProcessor::resize()` e un cambio di scala distruggono e ricreano il contesto FSR2.
- Il flag `reset` di FSR2 azzera la storia dopo schermate di caricamento, teletrasporti e salti della camera oltre 512 unità in un frame.
- Con stereo o multiview l'upscaler resta spento: le `Texture2DArray` non sono supportate dal port.

## Build

- Il port entra in `extern/fsr2-gl` come copia del sorgente, con la patch per `glad_glTextureSubImage*`.
- Gli shader FSR2 si compilano in SPIR-V durante il workflow "NerevarMoon Engine" (`.github/workflows/nerevarmoon.yml`).
- Opzione CMake `NM_FSR2`, accesa su Windows. Spenta, il codice dell'upscaler non compila e `upscaler = fsr2` scrive l'avviso del ripiego.
- `nm-upscaler` va aggiunto al trigger push del workflow per avere le build del ramo.

## Test

- WSL Debian con gtest, come `.superpowers/sdd/2026-10-17-point-light-shadows/localtest.sh`:
  - jitter: lo spostamento della proiezione vale `2 · offset / renderWidth` in NDC, contro valori calcolati a mano;
  - riproiezione: matrici note danno uno spostamento UV noto, con e senza reverse Z;
  - mip bias: `s = 0.67` dà circa -1.58.
- In gioco con nmtest e `fps_serie.ps1`, a Molag Ruhn e Seyda Neen, in tre configurazioni: upscaler spento, `fsr2` a 1.0, `fsr2` a 0.67. Il tempo GPU per frame si legge dalle stats OSG (`OPENMW_OSG_STATS_LIST=times`).
- Controllo a occhio di Lorenzo: scie dietro gli NPC che corrono, arma in prima persona durante il fendente, scintillio dell'erba rispetto a SMAA, effetti SSAO/HBAO che usano `omw.resolution`.

## Criteri di riuscita

- A `upscale scale = 0.67` il tempo GPU per frame scende rispetto all'upscaler spento.
- A `upscale scale = 1.0` l'erba scintilla meno che con SMAA, a giudizio di Lorenzo.
- Nessun crash e nessun errore GL nel log in 10 minuti di gioco.
- Con `upscaler = off` il frame è identico alla build 19.

Se il gioco risulta limitato dalla CPU, la scala 0.67 non aumenta gli fps e resta solo il guadagno di qualità.

## Fuori da questa spec

- Frame generation.
- Supporto stereo e VR.
- Scena HDR per FSR2.
