# SirCumcVFX: design (pilota Distruzione)

Data: 21/10/2026. Modlist: NerevarMoon (OpenMW 0.51, MO2, `F:\Modlists\NerevarMoon`).

## Obiettivo

Sostituire i VFX di "NerevarMoon - VFX" (disattivata il 21/10) con effetti nuovi per fuoco, gelo e shock.
Problemi dei vecchi VFX: poche particelle (massimo 65), texture da 128 px, poco movimento, scuole poco distinguibili.
Riferimenti visivi scelti da Lorenzo: scintille fisiche alla Dragon's Dogma 2; particelle dense, nebbie e bagliori saturi alla Oblivion Remastered.
Colori di base vanilla (fuoco rosso, gelo blu, shock viola), più saturi.

Criteri di riuscita:
1. Fuoco, gelo e shock si riconoscono senza leggere il nome dell'incantesimo.
2. 60 fps mantenuti nelle zone pesanti, con fuoco, gelo e shock lanciati insieme (misura F3, mod accesa e spenta).
3. Nessun errore in `openmw.log` legato alla mod.

## Ambito

Pilota: 3 elementi per 4 fasi (cast, bolt, hit, area) = 12 effetti.
Effetti di gioco toccati: Fire Damage, Frost Damage, Shock Damage. Nessun altro MGEF.
Le altre scuole arrivano dopo il pilota, con una spec propria.

## Aspetto

| | Fuoco | Gelo | Shock |
|---|---|---|---|
| Nucleo | bagliore arancio-bianco, pulsante | bagliore azzurro-bianco, fermo | lampo viola-bianco, a scatti |
| Particelle | braci che salgono, lingue di fiamma animate | cristalli che ruotano lenti | archi elettrici rapidi e frastagliati |
| Scintille | braci che cadono, rimbalzano, si spengono | schegge che rimbalzano e si frantumano | scintille bianche velocissime, 1 rimbalzo |
| Nebbia | fumo scuro che sale | vapore freddo che scende e striscia a terra | polvere ionizzata breve |
| Extra | distorsione da calore | nessuno | flash di luce più forte |
| Luce Lua | arancio caldo, 0,5 s | azzurro, 0,4 s | viola-bianco, 0,2 s, 2 sfarfallii |

Fasi: il cast raccoglie energia sulla mano; il bolt ha una scia; l'hit esplode con scintille; l'area è un'onda a terra con nebbia.

## Struttura della mod

Cartella MO2: `mods\SirCumcVFX`.

- `meshes\sircumcvfx\` : 12 nif, nomi `scv_<elemento>_<fase>.nif` (elemento: fire, frost, shock; fase: cast, bolt, hit, area). Nessun nif vanilla sovrascritto.
- `textures\sircumcvfx\` : flipbook DDS DXT5, da 256 a 512 px, generati da noi in Python (rumore procedurale). Nessun asset di terzi.
- `SirCumcVFX.omwaddon` : 12 STAT (`SCV_<Elemento>_<Fase>`) e override di 3 MGEF (Fire Damage, Frost Damage, Shock Damage) per i campi casting, bolt, hit e area visual. Gli altri campi dei 3 MGEF restano vanilla.
- `scripts\sircumcvfx\` : script Lua per le luci, registrati in `SirCumcVFX.omwscripts`.

## Generatore

`NerevarMoon-strumenti\nmtools\vfx2\`, in Python.

- Un file YAML per effetto: strati, numero di particelle, colori, durate, dimensioni, collider, extra `omw:data`.
- Lo script scrive i nif (NiParticleSystem con NiParticleSystemController, NiGravity, NiPlanarCollider, NiFlipController per i flipbook, NiAlphaProperty additiva o alpha blend).
- Genera anche le texture flipbook e un render di anteprima per ogni effetto (PNG e GIF), mandati a Lorenzo a ogni effetto creato.
- Prima del deploy un controllo blocca i nif che superano i tetti di prestazione.

## Funzioni del motore usate (verificate su openmw-0.51.0)

- `NiPlanarCollider` e `NiSphericalCollider` (nifloader.cpp:1221). Limite: il rimbalzo avviene su un piano fisso alla quota dell'effetto, la geometria reale del livello non conta.
- `NiFlipController` per le texture animate (nifloader.cpp:1126).
- `NiStringExtraData` `omw:data` (nifloader.cpp:700) con `soft_effect` (richiede `soft particles = true`) e `distortion` (richiede il post-processing attivo).
- Luci Lua con `types.Light.createRecordDraft` (light.cpp:148), poi `world.createRecord` e `world.createObject`.
- Text key del cast con `I.AnimationController.addTextKeyHandler` (animationcontroller.lua).
- I nif non emettono luce. Il motore dà una luce solo ai bolt (projectilemanager.cpp:139).

## Luci Lua

- Cast: luce sulla mano al text key `release` del gruppo `spellcast`, se l'incantesimo contiene uno dei 3 effetti.
- Hit su attore: luce sul bersaglio quando compare un nuovo effetto attivo di Fire, Frost o Shock Damage.
- Impatto a terra dell'area: luce solo se Lua 0.51 espone il punto d'impatto. Va verificato nel sorgente prima di scrivere lo script; se non lo espone, l'area resta senza luce Lua.
- Al massimo 4 luci insieme; la quinta spegne la più vecchia. Ogni luce viene rimossa allo scadere, anche se la cella si scarica.

## Tetti di prestazione

| Fase | Particelle massime |
|---|---|
| Cast | 80 |
| Bolt | 60 più la scia |
| Hit | 150 |
| Area | 300 |

Massimo 3 strati trasparenti per effetto. Texture massimo 512 px, totale sotto 16 MB.
Le particelle in OpenMW sono calcolate dalla CPU: i tetti proteggono i 60 fps nei combattimenti con molti incantatori.

## Degrado senza impostazioni

- Senza `soft particles`: la nebbia ha bordi netti dove tocca il terreno.
- Senza post-processing: la distorsione da calore sparisce.
- Gli effetti restano visibili in entrambi i casi.

## Test

Fuori dal gioco:
1. Ogni nif passa `niftest` (strumento di OpenMW che verifica la lettura del nif).
2. Il controllo del generatore conferma i tetti.
3. Lo script delle luci passa un test con API finta (lupa), come Combat Overhaul.
4. Render di anteprima approvati da Lorenzo, effetto per effetto.

In gioco (Lorenzo):
1. Fuoco, gelo e shock lanciati in una zona pesante; confronto fps con F3, mod accesa e spenta.
2. Controllo di `openmw.log`.

## Deploy

Regole NerevarMoon: MO2 chiuso, backup di modlist.txt, openmw.cfg, Morrowind.ini e loadorder.txt, fine riga CRLF, scrittura su `.tmp` e poi copia, verifica md5 dopo il deploy, voce nel registro modifiche.
Attivazione: `+SirCumcVFX` in modlist.txt, `data=` e `content=SirCumcVFX.omwaddon` più `content=SirCumcVFX.omwscripts` in openmw.cfg.

## Fuori ambito

- Altre scuole e altri effetti di Distruzione.
- Rimbalzo delle scintille sulla geometria reale.
- Cancellazione dei file di "NerevarMoon - VFX".
