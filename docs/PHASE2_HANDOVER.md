# Fas 2 - motståndare: allt jag hann ta reda på

Skriven vid överlämning 2026-08-16. Fas 1 är klar; det här är startpunkten för
nästa session så att kartläggningen inte behöver göras om.

## Kärninsikten: motståndarna är data, inte AI

`OPP1.PRE` .. `OPP6.PRE` innehåller 27 resurser var. De som betyder något:

| Resurs | Vad |
|:--|:--|
| `enam` | motståndarens namn |
| `path` | körlinjen genom banan |
| `sped` | hastighetstabell - 120, 75, 50, 40, 30, 20, 60, ... |
| `edes` | beskrivning |
| `ed1a`..`ed4a`, `ev1a`..`ev3c` | repliker och skryt |
| `winn`, `lose` | text vid vinst och förlust |

Porträtten ligger separat i `OPP<n>WIN.PVS` / `OPP<n>LOSE.PVS`, tre bilder om
120x79 vardera. **De är digitaliserade fotografier** och tål ingen
AI-uppskalning (mätt - se dokumentationen om uppskalning).

## Vad som ska portas

| Funktion | Källa | Rader |
|:--|:--|--:|
| `load_opponent_data` | seg004.asm | 266 |
| `opponent_op` | seg001.asm | 734 |
| `run_opponent_menu` | seg000.asm | 609 |

`load_opponent_data` hämtar `nam` med `locate_text_res` och `path`/`sped` med
`locate_shape_alt`, och har sedan ~200 rader bearbetning med stora
stackbuffertar (`var_B28` ligger på -2856) - den löser upp motståndarens
abstrakta linje mot den faktiska banlayouten. Det är den svåraste biten.

## Vad som redan finns på plats

* `state.opponentstate` finns i GAMESTATE och skickas redan in i
  `update_player_state()` (src/sim_faithful/state.c:50).
* `oppnentSped[16]` finns i `src/sim_faithful/sfdata.c:81` och **läses redan**
  av `sub_18D60` (sfasm_port.c:1047) som `oppnentSped[si_oppSpedCode +
  ss_surfaceType]`. Den är i dagsläget **nollfylld** - `sped`-resursen är
  precis den tabell som ska in där.
* Renderaren ritar redan en motståndarbil: `shape3d_load_car_shapes()` tar ett
  opponent-id och `car2resptr` finns.
* `followOpponentFlag` finns och används på flera ställen i frame.c.

## Vad som saknas i simuleringen

`player_op` (state.c) uppdaterar bara spelaren. Originalet anropar
`opponent_op` separat via `do_opponent_op` (seg001, 7 rader - en tunn thunk).
Där finns ingen motsvarighet i porten än.

Två attrapper i `src/sim_faithful/sfstubs.c` blir meningsfulla först nu:

* `car_car_coll_detect_maybe` - **anropas redan 208 gånger** per HELL5-inspelning
* `car_car_speed_adjust_maybe` - anropas inte i solokörning

## Arbetsmetod som gäller

Se `docs/FAITHFUL_RENDERER.md` och minnesanteckningen "verify-with-pixels".
Kort:

1. `--paint-check` före varje påstående om rendering
2. Kör riktiga inspelningar och titta på bilder, inte bara siffror
3. Kör varje ny 2D-bild genom `unflip_shape()`
4. Läs datan innan du skriver layoutkod - den här kodbasen lägger nästan allt i
   tillgångarna
5. **Kontrollera att en redigering faktiskt tog** innan du bygger

## Regressionskörning före varje leverans

```bash
for f in extracted/stunts/stunts/*.TRK; do
  t=$(basename "$f" .TRK)
  ./bin/stunts_native --data extracted/stunts/stunts --track "$t" --car coun --headless 120
done
./bin/stunts_native --data extracted/stunts/stunts \
  --replay tests/replays/00_default_hell5_full.rpl --paint-check
```

Fysiken ska vara bit-identisk och antalet omålade rutor 0.
