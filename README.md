# Elfa bailarina para AzerothCore

Modulo aparte del casino. NPC elfa (Selene). Le pagas oro, ella baila y las frases se vuelven mas intimas cuanto mas oro dejas en la sesion.

## Uso

1. Copia `mod-npc-dancer` a `azerothcore/modules/`.
2. Compila.
3. Copia `mod_npc_dancer.conf.dist` a configs.
4. Importa `data/sql/db-world/npc_dancer.sql` en world.
5. `.npc add 1001258`

## Tonos

| Propina | Tono |
|---|---|
| 10 oro | Juguetona |
| 15 oro | Coqueta |
| 20 oro | Sensual |
| 90 oro | Atrevida |
| 220 oro | Intima |

Si acumulas mucho oro en la misma sesion, anade una frase extra.

Al pagar, consulta `item_template` (`ORDER BY RAND()`) y te da un item. Mas oro = mejor Quality (comun a epico). Tambien baila, te mira, habla en voz alta de vez en cuando y suelta un efecto visual.

Usa los destellos de [mod-npc-services](https://github.com/azerothcore/mod-npc-services): hechizos `31726` y `59908`, mas una fortuna de Sayge (`23735`-`23769`) como aura de combate.
