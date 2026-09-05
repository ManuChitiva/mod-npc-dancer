#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "GossipDef.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "ScriptMgr.h"

#include <map>
#include <string>
#include <vector>

enum GossipMenuDancer
{
    GOSSIP_MENU_DANCER_WELCOME = 150180,
    GOSSIP_MENU_DANCER_TIER1   = 150181,
    GOSSIP_MENU_DANCER_TIER2   = 150182,
    GOSSIP_MENU_DANCER_TIER3   = 150183,
    GOSSIP_MENU_DANCER_TIER4   = 150184,
    GOSSIP_MENU_DANCER_TIER5   = 150185
};

enum DancerActions
{
    ACTION_TIP_1 = GOSSIP_ACTION_INFO_DEF + 1,
    ACTION_TIP_2 = GOSSIP_ACTION_INFO_DEF + 2,
    ACTION_TIP_3 = GOSSIP_ACTION_INFO_DEF + 3,
    ACTION_TIP_4 = GOSSIP_ACTION_INFO_DEF + 4,
    ACTION_TIP_5 = GOSSIP_ACTION_INFO_DEF + 5,
    ACTION_BACK  = GOSSIP_ACTION_INFO_DEF + 6
};

struct DancerSession
{
    uint64 totalCopper = 0;
    uint32 dances = 0;
    int32 lastTierLine[5] = { -1, -1, -1, -1, -1 };
    int32 lastExtra = -1;
    int32 lastHello = -1;
    uint32 lastItem = 0;
    uint32 lastItemCount = 0;
    std::string lastItemName;
};

static bool DancerEnable = true;
static bool DancerAnnounce = false;
static uint64 DancerTip[5] = { 100000, 150000, 200000, 900000, 2200000 };
static std::map<ObjectGuid, DancerSession> DancerSessions;

static std::string FormatGold(uint64 copper)
{
    return std::to_string(copper / 10000) + " oro";
}

static uint8 PrizeQualityForTier(uint8 tier)
{
    if (tier >= 4)
        return 4;
    if (tier >= 3)
        return 3;
    if (tier >= 2)
        return 2;
    return 1;
}

static bool GiveItemFromTemplate(Player* player, uint32 itemId, uint32 count, std::string& outName)
{
    QueryResult itemRow = WorldDatabase.Query("SELECT entry, name FROM item_template WHERE entry = {}", itemId);
    if (!itemRow)
        return false;

    Field* fields = itemRow->Fetch();
    uint32 dbEntry = fields[0].Get<uint32>();
    outName = fields[1].Get<std::string>();
    if (dbEntry != itemId)
        return false;

    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
    if (!proto)
        return false;

    ItemPosCountVec dest;
    InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, count);
    if (msg != EQUIP_ERR_OK)
    {
        player->SendEquipError(msg, nullptr, nullptr, itemId);
        return false;
    }

    Item* item = player->StoreNewItem(dest, itemId, true);
    if (!item)
        return false;

    player->SendNewItem(item, count, true, false);
    return true;
}

static bool RollGiftItem(Player* player, uint8 tier, uint32& itemId, uint32& itemCount, std::string& itemName)
{
    uint8 quality = PrizeQualityForTier(tier);
    uint32 playerLevel = player->GetLevel();

    for (uint8 attempt = 0; attempt < 10; ++attempt)
    {
        uint8 q = quality;
        if (attempt > 3 && q > 1)
            q = quality - 1;
        if (attempt > 6)
            q = 1;

        QueryResult result = WorldDatabase.Query(
            "SELECT entry, name, `class` FROM item_template "
            "WHERE Quality = {} "
            "AND `class` NOT IN (6, 10, 11, 12, 13) "
            "AND (Flags & 16) = 0 "
            "AND RequiredLevel <= {} "
            "AND name NOT LIKE '%DEPRECATED%' "
            "AND name NOT LIKE '%Test%' "
            "AND name NOT LIKE '%OLD%' "
            "AND name NOT LIKE '%NPC%' "
            "ORDER BY RAND() LIMIT 1",
            q, playerLevel);

        if (!result)
            continue;

        Field* fields = result->Fetch();
        itemId = fields[0].Get<uint32>();
        itemName = fields[1].Get<std::string>();
        uint32 itemClass = fields[2].Get<uint32>();
        itemCount = (itemClass == 7) ? urand(1, 5) : 1;

        if (GiveItemFromTemplate(player, itemId, itemCount, itemName))
            return true;
    }

    itemId = 0;
    itemCount = 0;
    itemName.clear();
    return false;
}

static std::string GiftWhisper(std::string const& name, std::string const& itemName, uint32 count)
{
    std::vector<std::string> lines = {
        " Toma, " + name + "... un detalle de mi parte: " + itemName + ".",
        " Esto te lo quito de mi cofre. " + itemName + " x" + std::to_string(count) + ".",
        " Un recuerdo para que no se te olvide esta danza: " + itemName + ".",
        " Guarda esto debajo de la almohada. " + itemName + "."
    };
    return lines[urand(0, lines.size() - 1)];
}

static uint8 TipTier(uint32 action)
{
    if (action >= ACTION_TIP_1 && action <= ACTION_TIP_5)
        return static_cast<uint8>(action - ACTION_TIP_1);
    return 0;
}

static std::string PickLine(std::vector<std::string> const& lines, int32& lastIndex)
{
    if (lines.empty())
        return "";
    if (lines.size() == 1)
        return lines[0];

    uint32 index = urand(0, lines.size() - 1);
    if (static_cast<int32>(index) == lastIndex)
        index = (index + 1 + urand(0, lines.size() - 2)) % lines.size();
    lastIndex = static_cast<int32>(index);
    return lines[index];
}

static std::string LineForTier(uint8 tier, std::string const& name, DancerSession& session)
{
    switch (tier)
    {
        case 0:
            return PickLine({
                "Que detalle, " + name + "... con esto me animo un poco.",
                "Mmm, un comienzo dulce. Mira como me muevo para ti.",
                "Gracias, cielo. Esto merece un baile corto... y una sonrisa.",
                "Un brillo de oro y ya me pico el cuerpo. Observa.",
                "Asi se empieza, " + name + ". Despacio... como me gusta.",
                "No es mucho, pero tu cara me vale. Bailo igual.",
                "Guardo esta moneda junto al corazon. Ahora mira mis caderas.",
                "Que educado. Te mereces un giro lento, solo para ti.",
                "Me haces sonreir. Eso ya es mitad del espectaculo.",
                "Si sigues dejando propinas, esta noche se pone interesante.",
                "Un suspiro por esa moneda. Ven, no muerdo... todavia.",
                "Me encanta cuando alguien aprecia el arte. Y el oro.",
                "Cierro los ojos un segundo... y cuando los abra, bailo.",
                "Tu nombre suena bonito con el tintineo del oro, " + name + ".",
                "Esto me basta para calentar. El resto... lo decides tu.",
                "Una flor, una moneda, un baile. Soy facil de convencer.",
                "Mira mis manos. Ahora imagina el resto del cuerpo al ritmo.",
                "Todavia estoy vestida de recato. Eso se arregla con mas oro.",
                "Gracias. Me pondre de puntillas para que me veas mejor.",
                "Que dulce eres cuando pagas. Sigue, que me gusta consentirte."
            }, session.lastTierLine[0]);
        case 1:
            return PickLine({
                "Asi me gusta, " + name + ". No apartes esos ojos de mi cintura.",
                "Tu oro me sienta bien. Ven, esta danza es solo tuya.",
                "Que generoso... si sigues asi, me pongo mas cerca.",
                "Siento el metal y se me pone la piel de gallina. Rico.",
                "Me recorro el cuello con los dedos... imagina que son los tuyos.",
                "Hoy tengo ganas de coquetear. Tu pagas, yo obedezco el ritmo.",
                "Un paso hacia ti. Otro. Dime si quieres que me detenga... no vas a querer.",
                "El vino esta frio. Yo no. Gracias a ti.",
                "Me encanta cuando me miras como si ya me tuvieras.",
                "Dejo caer un hombro. Solo un poco. Para que adivines.",
                "Si te acercas, oiras mi respiracion entre cada giro.",
                "Esta propina me da permiso para ser mala... un poquito.",
                "No mires al suelo, " + name + ". Arriba. Ahi esta el espectaculo.",
                "Me muerdo el labio cuando el oro cae. Costumbre.",
                "Bailo mas lento a proposito. Quiero que te impacientes.",
                "Tu generosidad me hace sentir deseada. Sigue hablando con monedas.",
                "Podria sonreirte toda la noche. O susurrarte. Tu eliges pagando.",
                "Me giro de espaldas... y se que no vas a parpadear.",
                "Hay perfume en mi muñeca. Si pagas mas, te dejo olerlo cerca.",
                "Eres facil de leer, " + name + ". Y me encanta lo que leo."
            }, session.lastTierLine[1]);
        case 2:
            return PickLine({
                "Siento tu mirada en la piel, " + name + "... y me encanta.",
                "Por eso me suelto el pelo. Dime al oido si quieres que baje mas el ritmo.",
                "Cada moneda tuya me hace olvidar que hay mas gente en la sala.",
                "Me arde el cuello. No es la chimenea.",
                "Quiero que mi sombra te cubra un momento. Dejame acercarme.",
                "Si pones la mano a un palmo, sentiras el calor que desprendo.",
                "Esta danza ya no es educada. Es hambre. Gracias por pagarla.",
                "Me recorro la cintura despacio para que sepas por donde irian tus dedos.",
                "Suspiro alto a proposito. Quiero que se te olvide el resto del mundo.",
                "El oro pesa bien en mi palma. Yo pesaria bien en tu regazo... si insistes.",
                "Me agacho, me incorporo. Cuenta los latidos. Van por ti.",
                "No preguntes si me gusta. Mira como se me pone la voz.",
                "Esta noche mi boca dice tu nombre entre dientes, " + name + ".",
                "Un hilo de sudor. Un hilo de oro. Los dos me quedan bien.",
                "Si sigues asi, voy a dejar de fingir que esto es solo un baile.",
                "Acercate a mi oido. Te dire que pieza de ropa imagino fuera.",
                "Me gusta cuando pagas sin hablar. El silencio se llena de intencion.",
                "Mis caderas no mienten, " + name + ". El oro tampoco.",
                "Tengo la boca seca de tanto mirarte. Otra moneda y me humedezco los labios.",
                "Bailo como si ya estuvieras conmigo. Porque en mi cabeza... lo estas."
            }, session.lastTierLine[2]);
        case 3:
            return PickLine({
                "Por esa cantidad me pongo a tu merced, " + name + ". Toca el aire... yo hago el resto.",
                "Estoy caliente, y no es la linterna. Quedate ahi y dejame bailarte encima.",
                "Si me pagas asi, esta noche soy tuya... al menos mientras suene la musica.",
                "Me deslizo hacia ti como si el suelo fuera tu cama.",
                "Quiero que oigas mi aliento. Esta irregular. Culpa tuya.",
                "Podria sentarme en el borde de esa silla... o en ti. El oro decide.",
                "Me recorro el muslo con la palma. Imagina la tuya encima.",
                "No voy a pedirte permiso para ser descarada. Ya lo pagaste.",
                "Mi voz baja porque esto no es para el resto. Es para tu oido.",
                "Si me tiembla la rodilla, no es el baile. Es lo que estoy imaginando.",
                "Deja el oro y mira. Quiero que recuerdes como me arqueo.",
                "Una noche cara. Un cuerpo dispuesto. Adivina cual soy yo.",
                "Me gusta cuando insistes con monedas gordas. Me pongo humeda de orgullo.",
                "Dime 'mas' con otra propina y yo contesto con las caderas.",
                "Estoy a un suspiro de olvidar que soy una artista y no tu amante.",
                "El corse aprieta. El oro afloja mis ideas. Sigue.",
                "Si me nombras despacio, " + name + ", te bailo como si me tocases.",
                "Quiero tu mirada en mi boca cuando gire. Ahi esta la promesa.",
                "Esta propina compra atrevimiento. Lo vas a notar en cada curva.",
                "No finjo modestia. Me gusta que pagues por verme rendida al ritmo."
            }, session.lastTierLine[3]);
        default:
            return PickLine({
                "Nadie me ha mimado como tu esta noche, " + name + ". Esto no se lo bailo a cualquiera...",
                "Acercate. Quiero que oigas como suspiro cuando el oro toca mi mesa.",
                "Eres peligroso, " + name + ". Con esto me quito el recato... y no pienso parar hasta que te canses de mirarme.",
                "Por eso me vuelvo lenta, profunda, casi indecente. Tu lo has comprado.",
                "Si esta sala estuviera vacia, este baile acabaria de otra forma.",
                "Me arrodillo un segundo, solo para que veas como te miro desde abajo.",
                "Gasta asi y me convierto en un secreto que no vas a contar.",
                "Quiero tu nombre en mi boca y tu oro en mi mesa. Ya tengo las dos cosas.",
                "Estoy temblando y no de frio. Tocame con la mirada otra vez.",
                "Esta es mi danza mas sucia. La guardo para quien paga como un rey.",
                "Si me pidieras quedarme cuando cierre... con esta propina lo pensaria en voz alta.",
                "Mi aliento te busca el cuello aunque no te toque. Aun.",
                "Me deshago del recato como de una capa. Gracias por el permiso en oro.",
                "Bailo contra el aire que hay entre nosotros. Ojala no hubiera aire.",
                "Eres mi vicio de esta noche, " + name + ". Y los vicios se pagan caros.",
                "Si el corazon se me dispara, echa la culpa a esa bolsa que acabas de abrir.",
                "Te lo susurro: me gusta ser observada cuando me entrego al ritmo.",
                "Una reina no se vende. Una elfa enamorada del oro... a veces si.",
                "Cierra los ojos. Cuando los abras, quiero estar mas cerca de lo que recuerdas.",
                "Esto ya no es un trato. Es un antojo. El tuyo y el mio."
            }, session.lastTierLine[4]);
    }
}

static std::string ExtraForSession(DancerSession& session, std::string const& name)
{
    uint64 gold = session.totalCopper / 10000;
    if (gold < 20)
        return "";

    if (gold >= 150)
        return PickLine({
            " Ya perdi la cuenta de lo que me has dado, " + name + "... y aun quiero mas de ti.",
            " Con lo que llevas gastado podrias comprarme la noche entera. No lo menciones... o si.",
            " Me tienes comprada la voz, las caderas y las ganas. Que peligroso eres.",
            " Si sigues asi, voy a olvidar que esto es un trabajo.",
            " Nadie ha sido tan insistente. Me gusta. Demasiado."
        }, session.lastExtra);

    if (gold >= 75)
        return PickLine({
            " Llevas mimandome tanto que me tiemblan las rodillas.",
            " Cada propina extra me hace mas tuya esta noche.",
            " Ya no finjo indiferencia. Tu bolsillo me ha desarmado.",
            " Sigo bailando porque tu no paras... y porque no quiero que pares.",
            " Me encanta como volviste. Otra vez. Y otra."
        }, session.lastExtra);

    return PickLine({
        " Cada vez que vuelves, me pongo mas suave contigo.",
        " Ya te conozco el ritmo. Y me gusta.",
        " No eres un cliente mas, " + name + ". Se te nota.",
        " Si sigues asi, voy a sonreirte de verdad.",
        " Me estas acostumbrando a ti. Eso es caro... y lo estas pagando."
    }, session.lastExtra);
}

static std::string HelloLine(DancerSession& session, std::string const& name)
{
    if (session.totalCopper / 10000 >= 50)
        return PickLine({
            "Otra vez tu... me alegro mas de lo que deberia, " + name + ".",
            "Ya senti tus pasos. El cuerpo se me adelanta a la boca.",
            "Vuelves. Bien. Todavia tengo calor de la ultima vez."
        }, session.lastHello);

    if (session.dances)
        return PickLine({
            "Te estaba esperando con una sonrisa torcida, " + name + ".",
            "No te habias ido del todo. Seguias en mi cabeza.",
            "Hola de nuevo. El oro anterior todavia me pica en los dedos."
        }, session.lastHello);

    return PickLine({
        "Pasa, " + name + ". Si traes oro, traes mi atencion.",
        "Bienvenido a mi rincon. Aqui la musica obedece a las monedas.",
        "Mmm... cara nueva, o cara que quiero volver a ver.",
        "No hace falta que hables. El oro habla por ti.",
        "Siéntate. O quedate de pie. Yo me encargo de que no te aburras."
    }, session.lastHello);
}

static uint32 GossipTextForTier(uint8 tier)
{
    switch (tier)
    {
        case 0: return GOSSIP_MENU_DANCER_TIER1;
        case 1: return GOSSIP_MENU_DANCER_TIER2;
        case 2: return GOSSIP_MENU_DANCER_TIER3;
        case 3: return GOSSIP_MENU_DANCER_TIER4;
        default: return GOSSIP_MENU_DANCER_TIER5;
    }
}

class npc_gold_dancer : public CreatureScript
{
public:
    npc_gold_dancer() : CreatureScript("npc_gold_dancer") { }

    struct npc_gold_dancerAI : public ScriptedAI
    {
        npc_gold_dancerAI(Creature* creature) : ScriptedAI(creature) { }

        uint32 danceRemain = 0;
        uint32 ambientTimer = 8000;

        void Reset() override
        {
            danceRemain = 0;
            ambientTimer = urand(8000, 14000);
            me->SetUInt32Value(UNIT_NPC_EMOTESTATE, 0);
        }

        void StartPaidDance(Player* player, uint32 durationMs)
        {
            danceRemain = durationMs;
            ambientTimer = durationMs + urand(4000, 8000);
            if (player)
                me->SetFacingToObject(player);
            me->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_STATE_DANCE);
            me->CastSpell(me, 31726, true);
            me->CastSpell(me, 44940, true);
        }

        void DoAmbient()
        {
            if (Player* nearPlayer = me->SelectNearestPlayer(20.0f))
                me->SetFacingToObject(nearPlayer);

            switch (urand(1, 5))
            {
                case 1: me->HandleEmoteCommand(EMOTE_ONESHOT_WAVE); break;
                case 2: me->HandleEmoteCommand(EMOTE_ONESHOT_SHY); break;
                case 3: me->HandleEmoteCommand(EMOTE_ONESHOT_KISS); break;
                case 4: me->HandleEmoteCommand(EMOTE_ONESHOT_LAUGH); break;
                default: me->HandleEmoteCommand(EMOTE_ONESHOT_FLEX); break;
            }

            switch (urand(1, 8))
            {
                case 1: me->Say("Alguien con oro y buen gusto... me aburro aqui sola.", LANG_UNIVERSAL); break;
                case 2: me->Say("Si me miras tanto, al menos deja una moneda.", LANG_UNIVERSAL); break;
                case 3: me->Say("La noche es corta. Yo no.", LANG_UNIVERSAL); break;
                case 4: me->Say("Un baile, un secreto, un poco de oro...", LANG_UNIVERSAL); break;
                case 5: me->Say("No muerdo. Bueno... casi nunca.", LANG_UNIVERSAL); break;
                case 6: me->Say("Ven. Te prometo que vale cada cobre.", LANG_UNIVERSAL); break;
                case 7: me->Say("Esta sala se siente vacia sin un cliente generoso.", LANG_UNIVERSAL); break;
                default: me->Say("Mmm... necesito musica. O un admirador.", LANG_UNIVERSAL); break;
            }
        }

        void UpdateAI(uint32 diff) override
        {
            if (danceRemain)
            {
                if (danceRemain <= diff)
                {
                    danceRemain = 0;
                    me->SetUInt32Value(UNIT_NPC_EMOTESTATE, 0);
                    me->HandleEmoteCommand(EMOTE_ONESHOT_KISS);
                    me->Say("Vuelve cuando quieras otro baile...", LANG_UNIVERSAL);
                }
                else
                    danceRemain -= diff;
                return;
            }

            if (ambientTimer <= diff)
            {
                DoAmbient();
                ambientTimer = urand(14000, 28000);
            }
            else
                ambientTimer -= diff;
        }
    };

    static void ShowMenu(Player* player, Creature* creature, uint32 textId)
    {
        DancerSession const& session = DancerSessions[player->GetGUID()];
        player->PlayerTalkClass->ClearMenus();

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|TInterface\\icons\\INV_Misc_Coin_01:40:40:-18|t Dejar " + FormatGold(DancerTip[0]) + " (juguetona)", GOSSIP_SENDER_MAIN, ACTION_TIP_1);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|TInterface\\icons\\INV_Misc_Coin_03:40:40:-18|t Dejar " + FormatGold(DancerTip[1]) + " (coqueta)", GOSSIP_SENDER_MAIN, ACTION_TIP_2);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|TInterface\\icons\\INV_Misc_Coin_05:40:40:-18|t Dejar " + FormatGold(DancerTip[2]) + " (sensual)", GOSSIP_SENDER_MAIN, ACTION_TIP_3);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|TInterface\\icons\\INV_Misc_Gem_Pearl_04:40:40:-18|t Dejar " + FormatGold(DancerTip[3]) + " (atrevida)", GOSSIP_SENDER_MAIN, ACTION_TIP_4);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|TInterface\\icons\\INV_Jewelry_Talisman_12:40:40:-18|t Dejar " + FormatGold(DancerTip[4]) + " (intima)", GOSSIP_SENDER_MAIN, ACTION_TIP_5);

        if (session.totalCopper)
            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "Esta noche me has dado " + FormatGold(session.totalCopper) + ".", GOSSIP_SENDER_MAIN, ACTION_BACK);
        if (session.lastItem)
            AddGossipItemFor(player, GOSSIP_ICON_VENDOR, "Ultimo regalo: " + session.lastItemName + " x" + std::to_string(session.lastItemCount), GOSSIP_SENDER_MAIN, ACTION_BACK);

        SendGossipMenuFor(player, textId, creature->GetGUID());
    }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (!DancerEnable)
        {
            CloseGossipMenuFor(player);
            return true;
        }

        ShowMenu(player, creature, GOSSIP_MENU_DANCER_WELCOME);
        DancerSession& session = DancerSessions[player->GetGUID()];
        creature->Whisper(HelloLine(session, player->GetName()).c_str(), LANG_UNIVERSAL, player);
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        if (!player || !creature || sender != GOSSIP_SENDER_MAIN)
            return false;

        if (action == ACTION_BACK)
        {
            ShowMenu(player, creature, GOSSIP_MENU_DANCER_WELCOME);
            return true;
        }

        uint8 tier = TipTier(action);
        uint64 price = DancerTip[tier];

        if (player->GetMoney() < price)
        {
            creature->Whisper("Sin oro no hay espectaculo, cielo. Vuelve cuando puedas mimarme.", LANG_UNIVERSAL, player);
            CloseGossipMenuFor(player);
            return true;
        }

        player->ModifyMoney(-static_cast<int32>(price));
        DancerSession& session = DancerSessions[player->GetGUID()];
        session.totalCopper += price;
        session.dances++;

        uint32 danceMs = 8000 + (tier * 4000);
        if (npc_gold_dancerAI* ai = CAST_AI(npc_gold_dancerAI, creature->AI()))
            ai->StartPaidDance(player, danceMs);
        else
        {
            creature->SetFacingToObject(player);
            creature->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_STATE_DANCE);
        }

        player->CastSpell(player, 47292, true);

        // Visuales de mod-npc-services: 31726 (restore glow) y 59908 (destello).
        player->CastSpell(player, 31726, true);
        creature->CastSpell(creature, 31726, true);
        if (tier >= 2)
            player->CastSpell(player, 59908, true);
        if (tier >= 3)
            player->CastSpell(player, 27741, true); // Love is in the Air

        static uint32 const Blessings[] = { 23735, 23736, 23737, 23738, 23766, 23767, 23768, 23769 };
        uint32 blessing = Blessings[urand(0, 7)];
        if (tier >= 3)
            blessing = (urand(0, 1) ? 23768 : 23737);
        player->CastSpell(player, blessing, true);

        std::string line = LineForTier(tier, player->GetName(), session);
        line += ExtraForSession(session, player->GetName());

        if (RollGiftItem(player, tier, session.lastItem, session.lastItemCount, session.lastItemName))
            line += GiftWhisper(player->GetName(), session.lastItemName, session.lastItemCount);

        creature->Whisper(line.c_str(), LANG_UNIVERSAL, player);
        ChatHandler(player->GetSession()).SendSysMessage(line.c_str());

        ShowMenu(player, creature, GossipTextForTier(tier));
        return true;
    }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_gold_dancerAI(creature);
    }
};

class DancerWorld : public WorldScript
{
public:
    DancerWorld() : WorldScript("DancerWorld", {
        WORLDHOOK_ON_BEFORE_CONFIG_LOAD
    }) { }

    void OnBeforeConfigLoad(bool /*reload*/) override
    {
        DancerEnable = sConfigMgr->GetOption<bool>("Dancer.Enable", true);
        DancerAnnounce = sConfigMgr->GetOption<bool>("Dancer.Announce", false);
        DancerTip[0] = sConfigMgr->GetOption<uint64>("Dancer.Tip1", 100000);
        DancerTip[1] = sConfigMgr->GetOption<uint64>("Dancer.Tip2", 150000);
        DancerTip[2] = sConfigMgr->GetOption<uint64>("Dancer.Tip3", 200000);
        DancerTip[3] = sConfigMgr->GetOption<uint64>("Dancer.Tip4", 900000);
        DancerTip[4] = sConfigMgr->GetOption<uint64>("Dancer.Tip5", 2200000);
    }
};

class DancerAnnounceScript : public PlayerScript
{
public:
    DancerAnnounceScript() : PlayerScript("DancerAnnounceScript", {
        PLAYERHOOK_ON_LOGIN
    }) { }

    void OnPlayerLogin(Player* player) override
    {
        if (DancerEnable && DancerAnnounce)
            ChatHandler(player->GetSession()).SendSysMessage("Este servidor tiene el modulo |cff4CFF00Danza privada|r.");
    }
};

void AddNpcGoldDancerScripts()
{
    new DancerWorld();
    new DancerAnnounceScript();
    new npc_gold_dancer();
}
