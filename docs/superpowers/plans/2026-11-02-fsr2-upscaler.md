# Upscaler FSR2 (fasi 1 a 3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** La scena del fork 0.51 renderizza a `upscale scale` della risoluzione e FSR2 (port OpenGL) la riporta a schermo pieno, con jitter, vettori della camera e reactive mask della prima persona.

**Architecture:** `PostProcessor` crea le texture della scena a dimensione ridotta e una `Tex_Upscaled` a dimensione piena. Una classe `Fsr2Upscaler` in `apps/openmw/mwrender/upscaler.cpp` esegue in GL grezzo due compute shader (velocità, reactive) e il dispatch FSR2 all'inizio di `PingPongCanvas::drawImplementation`. `RenderingManager` somma il jitter alla proiezione del mondo e della prima persona.

**Tech Stack:** C++20, OpenSceneGraph 3.6, OpenGL 4.6 compatibility, port FSR 2.2 GL (MIT), GLSL 460 compute, gtest.

**Spec:** `docs/superpowers/specs/2026-11-02-fsr2-upscaler-design.md`

La fase 4 della spec (vettori per oggetto) avrà un piano suo, dopo le prove in gioco della fase 3.

## Global Constraints

- Requisiti GL a runtime: OpenGL 4.6, `GL_ARB_gl_spirv`, `GL_KHR_shader_subgroup`.
- Chiavi in `[Post Processing]`: `upscaler` (`off`, `fsr2`, default `off`), `upscale scale` (0.5 .. 1.0, default 0.67), `upscale sharpness` (0.0 .. 1.0, default 0.3), `upscale mip bias` (`auto` o numero, default `auto`).
- `auto` vale `log2(s) - 1`.
- Con `upscaler = fsr2`, MSAA forzato a 0.
- Ripiego: messaggio di log esatto `[upscaler] fsr2 unavailable: <motivo>`, poi scala 1.0 e upscaler spento.
- Reset della storia: salto della camera oltre 512 unità in un frame.
- Stereo o multiview: upscaler sempre spento.
- Opzione CMake `NM_FSR2`, accesa su Windows.
- Con `upscaler = off` il frame resta identico alla build 19.
- Shader installati del motore (`resources/shaders`) non si toccano: i compute shader nuovi stanno come stringhe nel `.cpp`.
- Convenzione OSG: vettori riga, `v * M`.

## Review Focus

1. Verso Y dei vettori: GL ha l'origine UV in basso a sinistra. Un giro di camera orizzontale e uno verticale non devono lasciare scie. Test in Task 5, Step 6.
2. Segno del jitter: il valore passato a FSR2 deve corrispondere allo spostamento sommato alla proiezione. Fermi a scala 1.0, l'immagine deve restare nitida e immobile. Test in Task 5, Step 6.
3. Resize della finestra, alt-tab, cambio di risoluzione nelle opzioni: il contesto FSR2 si ricrea senza crash e senza errori GL. Test in Task 5, Step 6.
4. Camere secondarie (riflessione dell'acqua, ombre) devono restare senza jitter: l'acqua non deve tremolare. Test in Task 4, Step 4.
5. Gioco in pausa e menu aperti (dt quasi 0, camera ferma): l'immagine resta nitida, l'HUD resta a risoluzione piena anche a scala 0.5. Test in Task 3, Step 5 e Task 5, Step 6.

## Ciclo di build

Il motore si compila solo nel workflow "NerevarMoon Engine" (circa 50 minuti per push). I test puri girano in WSL Debian con `localtest.sh`. Si installano solo gli 11 exe, `resources/version` e `CI-ID.txt`, con backup e md5 in `NerevarMoon-strumenti/backup_engine_20261015/`. Le prove in gioco usano nmtest, oppure Lorenzo davanti al PC per i controlli a occhio. Più task si possono raggruppare in un solo push.

---

### Task 1: Port FSR2 GL dentro il build

**Files:**
- Create: `extern/fsr2-gl/` (sorgente del port, solo `src/ffx-fsr2-api` più gli shader GL e la licenza)
- Create: `extern/fsr2-gl/CMakeLists.txt`
- Modify: `extern/CMakeLists.txt`
- Modify: `CMakeLists.txt` (opzione `NM_FSR2`)
- Modify: `apps/openmw/CMakeLists.txt` (link e define `NM_FSR2`)
- Modify: `.github/workflows/nerevarmoon.yml` (trigger push su `nm-upscaler`)

**Interfaces:**
- Produces: target CMake `fsr2gl` (libreria statica). Header `ffx_fsr2.h` e `gl/ffx_fsr2_gl.h` sotto l'include `extern/fsr2-gl/src/ffx-fsr2-api`. Define `NM_FSR2=1` per `openmw` quando l'opzione è accesa.

- [ ] **Step 1: Copiare il port** da github.com/JuanDiegoMontoya/FidelityFX-FSR2 a un commit fisso. Il commit va scritto in `extern/fsr2-gl/VERSION.txt`. Il download richiede il permesso di Lorenzo.
- [ ] **Step 2: Trovare come il port compila gli shader GL** (cartella `build/`, `tools/sc`, CMake del port) e riprodurlo nel `CMakeLists.txt` nuovo. Se il port ha già gli header SPIR-V generati, si usano quelli.
- [ ] **Step 3: Patch `glad_glTextureSubImage1D/2D/3D`** in `ffx_fsr2_gl.cpp` verso i nomi della tabella `glFunctionTable`.
- [ ] **Step 4: Opzione `NM_FSR2`**, default `ON` se `WIN32`, altrimenti `OFF`. Con `OFF` il target `fsr2gl` non esiste.
- [ ] **Step 5: Aggiungere `nm-upscaler` al trigger push** di `nerevarmoon.yml`.
- [ ] **Step 6: Push e build.** Atteso: run verde, log con `fsr2gl` compilato e linkato in `openmw.exe`.
- [ ] **Step 7: Commit** `build: vendor FSR2 OpenGL port behind NM_FSR2`.

### Task 2: Matematica pura e impostazioni

**Files:**
- Create: `components/sceneutil/upscalemath.hpp`, `components/sceneutil/upscalemath.cpp`
- Create: `apps/components_tests/sceneutil/testupscalemath.cpp`
- Create: `.superpowers/sdd/2026-11-02-fsr2-upscaler/localtest.sh` (modello: `_nm_work/openmw/.superpowers/sdd/2026-10-17-point-light-shadows/localtest.sh`, con `cd` su `openmw-vfxoffset`)
- Modify: `components/CMakeLists.txt` (aggiungere `upscalemath` alla lista `sceneutil`), `apps/components_tests/CMakeLists.txt:92` (nuovo test)
- Modify: `components/settings/categories/postprocessing.hpp`, `files/settings-default.cfg:1268`

**Interfaces:**
- Produces, in `namespace SceneUtil`:
  - `float upscaleMipBias(float scale);`
  - `std::optional<float> parseMipBias(std::string_view value);` (`"auto"` e testo non valido danno `std::nullopt`)
  - `osg::Vec2i upscaleSceneSize(osg::Vec2i outputSize, float scale);` (arrotonda, minimo 1 per lato)
  - `osg::Vec2f jitterToNdc(osg::Vec2f offsetPixels, osg::Vec2i sceneSize);`
  - `osg::Vec2f reprojectUv(osg::Vec2f uv, float depth, const osg::Matrixf& invViewProj, const osg::Matrixf& prevViewProj, bool reverseZ);` (ritorna `prevUv - uv`)
  - `bool isCameraCut(const osg::Vec3f& prevPos, const osg::Vec3f& pos, float threshold = 512.f);`
- Produces, in `Settings::PostProcessingCategory`: `mUpscaler` (`std::string`, `makeEnumSanitizerString({ "off", "fsr2" })`), `mUpscaleScale` (`float`, `makeClampSanitizerFloat(0.5f, 1.f)`), `mUpscaleSharpness` (`float`, `makeClampSanitizerFloat(0.f, 1.f)`), `mUpscaleMipBias` (`std::string`).

- [ ] **Step 1: Scrivere i test** in `testupscalemath.cpp`:

```cpp
TEST(SceneUtilUpscaleMath, mipBiasAuto) { EXPECT_NEAR(SceneUtil::upscaleMipBias(0.67f), -1.5778f, 1e-3f); }
TEST(SceneUtilUpscaleMath, mipBiasAtFullScale) { EXPECT_FLOAT_EQ(SceneUtil::upscaleMipBias(1.f), -1.f); }
TEST(SceneUtilUpscaleMath, parseMipBias)
{
    EXPECT_FALSE(SceneUtil::parseMipBias("auto").has_value());
    EXPECT_FALSE(SceneUtil::parseMipBias("abc").has_value());
    EXPECT_FLOAT_EQ(*SceneUtil::parseMipBias("-0.5"), -0.5f);
}
TEST(SceneUtilUpscaleMath, sceneSize)
{
    EXPECT_EQ(SceneUtil::upscaleSceneSize({ 1920, 1080 }, 0.67f), osg::Vec2i(1286, 724));
    EXPECT_EQ(SceneUtil::upscaleSceneSize({ 1, 1 }, 0.5f), osg::Vec2i(1, 1));
}
TEST(SceneUtilUpscaleMath, jitterToNdc)
{
    const osg::Vec2f j = SceneUtil::jitterToNdc({ 0.25f, -0.5f }, { 1286, 724 });
    EXPECT_NEAR(j.x(), 0.5f / 1286.f, 1e-7f);
    EXPECT_NEAR(j.y(), -1.f / 724.f, 1e-7f);
}
TEST(SceneUtilUpscaleMath, reprojectStaticCameraIsZero)
{
    const osg::Matrixf proj = osg::Matrixf::perspective(60.0, 16.0 / 9.0, 1.0, 10000.0);
    const osg::Matrixf inv = osg::Matrixf::inverse(proj);
    for (bool rz : { false, true })
    {
        const osg::Vec2f d = SceneUtil::reprojectUv({ 0.3f, 0.7f }, 0.9f, inv, proj, rz);
        EXPECT_NEAR(d.x(), 0.f, 1e-5f);
        EXPECT_NEAR(d.y(), 0.f, 1e-5f);
    }
}
TEST(SceneUtilUpscaleMath, reprojectCameraMovedRight)
{
    // current view is identity; in the previous view space the same point sat at x = +10
    const osg::Matrixf proj = osg::Matrixf::perspective(90.0, 1.0, 1.0, 10000.0);
    const osg::Matrixf prevView = osg::Matrixf::translate(10.f, 0.f, 0.f);
    const osg::Matrixf inv = osg::Matrixf::inverse(proj);
    // point at view depth 100 in the centre: NDC z for unreversed perspective
    const osg::Vec4f clip = osg::Vec4f(0.f, 0.f, -100.f, 1.f) * proj;
    const float depth = (clip.z() / clip.w()) * 0.5f + 0.5f;
    const osg::Vec2f d = SceneUtil::reprojectUv({ 0.5f, 0.5f }, depth, inv, prevView * proj, false);
    EXPECT_NEAR(d.x(), 0.05f, 1e-4f); // 10/100 in NDC = 0.1, in UV = 0.05
    EXPECT_NEAR(d.y(), 0.f, 1e-5f);
}
TEST(SceneUtilUpscaleMath, cameraCut)
{
    EXPECT_FALSE(SceneUtil::isCameraCut({ 0, 0, 0 }, { 300, 0, 0 }));
    EXPECT_TRUE(SceneUtil::isCameraCut({ 0, 0, 0 }, { 513, 0, 0 }));
}
```

- [ ] **Step 2: Lanciare** `wsl -d Debian bash .superpowers/sdd/2026-11-02-fsr2-upscaler/localtest.sh`. Atteso: errore di compilazione, `upscalemath.hpp` mancante.
- [ ] **Step 3: Implementare** `upscalemath.cpp`. In `reprojectUv`, NDC z vale `depth` con reverse Z (clip control zero-to-one) e `depth * 2 - 1` senza. La matrice `invViewProj` del test è solo la proiezione inversa: la funzione non distingue view e projection.
- [ ] **Step 4: Rilanciare `localtest.sh`.** Atteso: 8 test PASS.
- [ ] **Step 5: Impostazioni.** Quattro `SettingValue` in `postprocessing.hpp`, quattro righe con commento in `settings-default.cfg` sotto `[Post Processing]`, con i default delle Global Constraints.
- [ ] **Step 6: Commit** `sceneutil: upscale math and post processing upscaler settings`.

### Task 3: Render scale

**Files:**
- Modify: `apps/openmw/mwrender/postprocessor.hpp` (enum `Tex_Upscaled`, `Tex_FirstPersonDepth`; metodi nuovi)
- Modify: `apps/openmw/mwrender/postprocessor.cpp:124-125, 230-245, 296-303, 436-535, 862-874`
- Modify: `apps/openmw/mwrender/renderingmanager.cpp:1155-1163` (`setScreenRes` con la dimensione della scena)

**Interfaces:**
- Consumes: `SceneUtil::upscaleSceneSize`, `Settings::postProcessing().mUpscaler`, `mUpscaleScale`.
- Produces, in `PostProcessor`:
  - `bool upscalerActive() const;` (vero con `mUpscaler == "fsr2"`, senza stereo, senza ripiego scattato)
  - `int sceneWidth() const; int sceneHeight() const;` (uguali a `renderWidth()`/`renderHeight()` se `upscalerActive()` è falso)
  - `void disableUpscaler(std::string_view reason);` (scrive il log del ripiego, rimette scala 1.0, chiama `resize()`)
  - texture `Tex_Upscaled` RGBA16F a `renderWidth() × renderHeight()`, flag `GL_TEXTURE_2D` immutabile non richiesto.

- [ ] **Step 1: `sceneWidth()`/`sceneHeight()`** e `upscalerActive()`. Con upscaler attivo, `mSamples` vale 0 nel costruttore.
- [ ] **Step 2: Texture e FBO della scena** in `createObjectsForFrame` a `sceneWidth() × sceneHeight()`. `renderWidth()` resta la dimensione d'uscita per le tecniche omwfx (riga 656) e per `Tex_Upscaled`. Viewport della camera principale a dimensione scena in `resize()`.
- [ ] **Step 3: Percorso provvisorio.** Prima del disegno della catena, `glBlitFramebuffer` lineare da `Tex_Scene` a `Tex_Upscaled`. Il canvas riceve `Tex_Upscaled` in `setTextureScene` (riga 301) quando l'upscaler è attivo. Il blit sparisce in Task 5.
- [ ] **Step 4: Build, installazione, prova con nmtest** a Seyda Neen con `upscaler = fsr2` e `upscale scale = 0.5`. Atteso: immagine sfocata ma intera, nessuna striscia nera, nessun `GL error` in `openmw.log`.
- [ ] **Step 5: Controllo Review Focus 5.** Inventario aperto e menu di pausa a scala 0.5: testo e HUD nitidi a 1080p.
- [ ] **Step 6: Controllo `upscaler = off`.** Screenshot F12 a Seyda Neen confrontato con la build 19 nello stesso punto: pixel identici.
- [ ] **Step 7: Commit** `postprocessor: render scene at upscale scale`.

### Task 4: Jitter

**Files:**
- Modify: `apps/openmw/mwrender/renderingmanager.hpp:284-287, 364`, `renderingmanager.cpp:651-708, 1137-1146`
- Modify: `apps/openmw/mwrender/npcanimation.cpp:385-391` (`OverrideFieldOfViewCallback`)
- Modify: `apps/openmw/mwrender/postprocessor.hpp`

**Interfaces:**
- Consumes: `SceneUtil::jitterToNdc`, `ffxFsr2GetJitterPhaseCount(int32_t renderWidth, int32_t displayWidth)`, `ffxFsr2GetJitterOffset(float* x, float* y, int32_t index, int32_t phaseCount)`, `PostProcessor::upscalerActive()`, `sceneWidth()`.
- Produces:
  - `osg::Vec2f RenderingManager::getJitterNdc() const;`
  - `osg::Vec2f PostProcessor::getJitterPixels() const;` e `void PostProcessor::setJitterPixels(osg::Vec2f);`
  - `osg::Matrixf PostProcessor::getUnjitteredViewProjection() const;` e la stessa del frame prima, `getPrevUnjitteredViewProjection()`.

- [ ] **Step 1: Jitter per frame** in `RenderingManager::update`. Con `upscalerActive()`: indice = numero di frame modulo `ffxFsr2GetJitterPhaseCount(sceneWidth, renderWidth)`, offset da `ffxFsr2GetJitterOffset`, poi `jitterToNdc` e `updateProjectionMatrix()`. Con upscaler spento, jitter zero e nessuna chiamata in più.
- [ ] **Step 2: Sommare `getJitterNdc()`** alla traslazione in `updateProjectionMatrix` (righe 1137-1146) e in `OverrideFieldOfViewCallback` (righe 385-391). La matrice senza jitter va salvata in `PostProcessor` prima della somma, insieme a quella del frame prima.
- [ ] **Step 3: Build, prova a scala 1.0** con il blit di Task 3. Atteso: immagine che vibra di meno di un pixel sia nel mondo sia su braccia e arma.
- [ ] **Step 4: Controllo Review Focus 4.** Acqua a Seyda Neen ferma: riflessi senza tremolio in più rispetto a `upscaler = off`. Se le camere della riflessione ereditano il jitter, quelle camere ricevono la matrice senza jitter.
- [ ] **Step 5: Commit** `rendering: sub-pixel projection jitter for the upscaler`.

### Task 5: Fsr2Upscaler

**Files:**
- Create: `apps/openmw/mwrender/upscaler.hpp`, `apps/openmw/mwrender/upscaler.cpp`
- Modify: `apps/openmw/CMakeLists.txt` (file nuovi in `mwrender`, solo con `NM_FSR2`)
- Modify: `apps/openmw/mwrender/postprocessor.cpp:495-535` (profondità della prima persona come texture), `postprocessor.cpp:230-245` (ricreazione su resize)
- Modify: `apps/openmw/mwrender/pingpongcanvas.hpp`, `pingpongcanvas.cpp:73` (chiamata del dispatch, via del blit)

**Interfaces:**
- Consumes: tutto quanto prodotto da Task 2, 3 e 4. `osg::getGLExtensionFuncPtr(const char*)` come `getProcAddress` del port.
- Produces, in `namespace MWRender`:

```cpp
class Fsr2Upscaler
{
public:
    struct Inputs
    {
        GLuint mColor, mDepth, mFirstPersonDepth, mOutput;
        osg::Vec2i mSceneSize, mOutputSize;
        osg::Vec2f mJitterPixels;
        osg::Matrixf mInvViewProj, mPrevViewProj;
        float mFrameTimeMs, mSharpness, mNear, mFar, mFovY;
        bool mReverseZ, mReset;
    };
    static std::string checkSupport(); // empty when GL 4.6 + GL_ARB_gl_spirv + GL_KHR_shader_subgroup are there
    std::string create(osg::Vec2i sceneSize, osg::Vec2i outputSize, bool reverseZ); // empty on success
    void destroy();
    void dispatch(osg::State& state, const Inputs& inputs);
};
```

- [ ] **Step 1: Profondità della prima persona.** Con upscaler attivo, `FBO_FirstPerson` usa `Tex_FirstPersonDepth` (texture depth-stencil a dimensione scena) al posto del renderbuffer delle righe 495-500.
- [ ] **Step 2: `checkSupport()` e `create()`.** Versione da `glGetString(GL_VERSION)`, estensioni con `glGetStringi`. `create` alloca lo scratch con `ffxFsr2GetScratchMemorySizeGL()`, chiama `ffxFsr2GetInterfaceGL` e `ffxFsr2ContextCreate` con `FFX_FSR2_ENABLE_DEPTH_INVERTED` se `reverseZ`, crea `Tex_Velocity` (RG16F) e `Tex_Reactive` (R8) e compila i due compute shader. Ogni errore torna come testo per `disableUpscaler`.
- [ ] **Step 3: I due compute shader**, GLSL 460, gruppi 8×8, come stringhe in `upscaler.cpp`. Il shader velocità applica la stessa formula di `SceneUtil::reprojectUv`. Il shader reactive scrive 1.0 dove `Tex_FirstPersonDepth` differisce dal valore di clear (0.0 con reverse Z, 1.0 senza).
- [ ] **Step 4: `dispatch()`.** Ordine: velocità, reactive, `glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT)`, `ffxFsr2ContextDispatch` con `motionVectorScale = (sceneW, sceneH)`, `jitterOffset = mJitterPixels`, `sharpness`, `enableSharpening = sharpness > 0`, `reset`. Poi pulizia dello stato: `glUseProgram(0)`, `glBindSampler(u, 0)` e `glBindImageTexture(u, 0, ...)` per `u` da 0 a 15, `state.setLastAppliedProgramObject(nullptr)`, `state.dirtyAllAttributes()`, `state.dirtyAllModes()`.
- [ ] **Step 5: Aggancio.** `PostProcessor` possiede un `Fsr2Upscaler` e lo ricrea in `resize()`. `PingPongCanvas::drawImplementation` chiama il dispatch in testa al posto del blit di Task 3. `mReset` vale vero al primo frame dopo `create`, quando `SceneUtil::isCameraCut` scatta sulla posizione della camera, e quando il frame precedente è durato più di 500 ms (schermate di caricamento). Un fallimento di `checkSupport` o `create` chiama `disableUpscaler`.
- [ ] **Step 6: Build e prove in gioco**, con Lorenzo per i controlli a occhio:
  - Review Focus 1: giro di camera orizzontale e verticale a Seyda Neen a scala 0.67, nessuna scia sugli edifici.
  - Review Focus 2: fermi a scala 1.0 per 10 secondi, immagine nitida e immobile.
  - Review Focus 3: alt-tab, cambio di risoluzione dalle opzioni, nessun crash e nessun `GL error` nel log.
  - Review Focus 5: menu di pausa aperto 10 secondi, immagine che non deriva.
  - Arma in prima persona durante il fendente: nessuna scia lunga.
- [ ] **Step 7: Prova del ripiego.** Avvio con `upscaler = fsr2` e la variabile d'ambiente `OPENMW_NM_FSR2_FAIL=1`, che fa fallire `checkSupport`. Atteso: riga `[upscaler] fsr2 unavailable: forced by OPENMW_NM_FSR2_FAIL` nel log e gioco a scala 1.0.
- [ ] **Step 8: Commit** `mwrender: FSR2 upscaler with camera motion vectors and first person reactive mask`.

### Task 6: Mip bias, misure e registro

**Files:**
- Modify: `apps/openmw/mwrender/renderingmanager.cpp` (stateset radice)
- Modify: `F:\Modlists\NerevarMoon\Downloads\NerevarMoon-strumenti\LEGGIMI - Registro modifiche NerevarMoon.md`

**Interfaces:**
- Consumes: `SceneUtil::parseMipBias`, `SceneUtil::upscaleMipBias`, `PostProcessor::upscalerActive()`.

- [ ] **Step 1: Mip bias.** Con upscaler attivo, un `osg::TexEnvFilter` con `parseMipBias(mUpscaleMipBias).value_or(upscaleMipBias(scale))` sulle texture unit 0..7 dello stateset radice della scena. Con upscaler spento, nessun attributo.
- [ ] **Step 2: Build e prova a occhio.** Pavimento di legno a Seyda Neen a scala 0.67: dettaglio della texture vicino a quello con upscaler spento.
- [ ] **Step 3: Misure** con `nmtools\fps_serie.ps1` a Molag Ruhn e Seyda Neen in tre configurazioni: `upscaler = off`, `fsr2` a 1.0, `fsr2` a 0.67, con `OPENMW_OSG_STATS_LIST=times`. Nelle due configurazioni `fsr2` SMAA va tolto dalla catena omwfx. Atteso per la spec: tempo GPU per frame più basso a 0.67 rispetto a `off`.
- [ ] **Step 4: Controllo di Lorenzo**: erba a 1.0 contro SMAA, scie dietro gli NPC che corrono (attese fino alla fase 4).
- [ ] **Step 5: Registro.** Riga nel registro modifiche con build, impostazioni e misure.
- [ ] **Step 6: Commit** `rendering: upscale mip bias`.
