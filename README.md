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
| 1 oro | Juguetona |
| 5 oro | Coqueta |
| 10 oro | Sensual |
| 25 oro | Atrevida |
| 50 oro | Intima |

Si acumulas mucho oro en la misma sesion, anade una frase extra.
