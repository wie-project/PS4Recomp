// libSceNpToolkit2 HLE implementation based on shadPS4 aerolib model.
#include "ps4_nptoolkit2.h"
#include "../../core/recomp_runtime.h"
#include <stdio.h>
#include <string.h>

typedef enum NpToolkit2Type {
    TK_CTOR,
    TK_DTOR,
    TK_GET,
    TK_METHOD_OK,
} NpToolkit2Type;

typedef struct NpToolkit2Entry {
    const char *nid;
    const char *demangled;
    NpToolkit2Type type;
} NpToolkit2Entry;

static const NpToolkit2Entry s_toolkit_entries[] = {
    {"+LVSE3Fjofw", "_ZN3sce7Toolkit2NP2V211UserProfile9NpProfileD1Ev", TK_DTOR},
    {"-A0EiAdM3tI", "_ZN3sce7Toolkit2NP2V26Trophy7Request6UnlockD1Ev", TK_DTOR},
    {"-bAjEe-hdCg", "_ZN3sce7Toolkit2NP2V24Core7Request10InitParamsD1Ev", TK_DTOR},
    {"-p97-goGFC4", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_8Commerce19ServiceEntitlementsEE3getEv", TK_GET},
    {"0+LtsbPVpt8", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_12NetworkUtils12Notification14NetStateChangeEE3getEv", TK_GET},
    {"04SGUwIDmfE", "_ZN3sce7Toolkit2NP2V26Trophy7Request19GetUnlockedTrophiesD1Ev", TK_DTOR},
    {"16uyavDW06I", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_8Presence12Notification14PresenceUpdateEE3getEv", TK_GET},
    {"2eQ9LLJMx5Q", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_4Auth8AuthCodeEEC1Ev", TK_CTOR},
    {"2ho2X0o+LGc", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_4Auth7IdTokenEEC1Ev", TK_CTOR},
    {"30Zxe35LtBg", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS3_5EmptyEED1Ev", TK_DTOR},
    {"31Bh5lfPPXs", "_ZN3sce7Toolkit2NP2V211UserProfile7Request13GetNpProfilesC1Ev", TK_CTOR},
    {"4mdynkqoCFc", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_6Friend7FriendsEE3getEv", TK_GET},
    {"5-UHpg8s00I", "_ZN3sce7Toolkit2NP2V24Core12ResponseBase13getReturnCodeEv", TK_METHOD_OK},
    {"589Hj6gCFu0", "_ZN3sce7Toolkit2NP2V26Trophy19getUnlockedTrophiesERKNS3_7Request19GetUnlockedTrophiesEPNS2_4Core8ResponseINS3_16UnlockedTrophiesEEE", TK_METHOD_OK},
    {"5eo3yRQ09IU", "_ZN3sce7Toolkit2NP2V26Trophy7Request19GetUnlockedTrophiesC1Ev", TK_CTOR},
    {"5gP3fp-B64Y", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_6Friend12Notification16FriendlistUpdateEE3getEv", TK_GET},
    {"6esKXS17kNM", "_ZN3sce7Toolkit2NP2V211UserProfile7Request13GetNpProfilesD1Ev", TK_DTOR},
    {"7Te1eeupXxQ", "_ZN3sce7Toolkit2NP2V26Friend15getBlockedUsersERKNS3_7Request15GetBlockedUsersEPNS2_4Core8ResponseINS3_12BlockedUsersEEE", TK_METHOD_OK},
    {"7zsee5IHLis", "_ZN3sce7Toolkit2NP2V26Friend10getFriendsERKNS3_7Request10GetFriendsEPNS2_4Core8ResponseINS3_7FriendsEEE", TK_METHOD_OK},
    {"83lbPafRjgU", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_6Trophy16UnlockedTrophiesEED1Ev", TK_DTOR},
    {"8E8iEeuyUks", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_6Trophy16UnlockedTrophiesEE3getEv", TK_GET},
    {"8Mio51rNcJQ", "_ZN3sce7Toolkit2NP2V26Trophy7Request19GetTrophyPackTrophyC1Ev", TK_CTOR},
    {"8b2o8RfK620", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_8Commerce19ServiceEntitlementsEEC1Ev", TK_CTOR},
    {"8qtytWLvK3c", "_ZN3sce7Toolkit2NP2V24Auth7Request11GetAuthCodeC1Ev", TK_CTOR},
    {"9G7fpmodu1Q", "_ZN3sce7Toolkit2NP2V28Commerce22getServiceEntitlementsERKNS3_7Request22GetServiceEntitlementsEPNS2_4Core8ResponseINS3_19ServiceEntitlementsEEE", TK_METHOD_OK},
    {"9ao-QZCm5RI", "_ZN3sce7Toolkit2NP2V28Commerce7Request21DisplayCheckoutDialogC1Ev", TK_CTOR},
    {"A-MGi5icglU", "_ZN3sce7Toolkit2NP2V24Auth7Request10GetIdTokenD1Ev", TK_DTOR},
    {"A0Ts3Oz6TPo", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_6Friend12Notification15BlocklistUpdateEE3getEv", TK_GET},
    {"AMMz7TNLoDg", "_ZN3sce7Toolkit2NP2V28Commerce7Request29DisplayVoucherCodeInputDialogC1Ev", TK_CTOR},
    {"AUW6at2PxHs", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_6Friend12BlockedUsersEE3getEv", TK_GET},
    {"AiQVTEwWQ5w", "_ZN3sce7Toolkit2NP2V26Trophy7Request19GetTrophyPackTrophyD1Ev", TK_DTOR},
    {"AleDTt+SyDo", "_ZN3sce7Toolkit2NP2V26Friend7Request15GetBlockedUsersC1Ev", TK_CTOR},
    {"CU6pVH4LNf8", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_8Commerce10CategoriesEED1Ev", TK_DTOR},
    {"DZVm+Cr-uB4", "_ZN3sce7Toolkit2NP2V28Commerce13getCategoriesERKNS3_7Request13GetCategoriesEPNS2_4Core8ResponseINS3_10CategoriesEEE", TK_METHOD_OK},
    {"E-GDXpncS2I", "_ZN3sce7Toolkit2NP2V28Commerce7Request13GetCategoriesC1Ev", TK_CTOR},
    {"EEvugJCBmb8", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_4Auth7IdTokenEED1Ev", TK_DTOR},
    {"ESmRsPD6RRs", "_ZN3sce7Toolkit2NP2V26Trophy20getTrophyPackSummaryERKNS3_7Request20GetTrophyPackSummaryEPNS2_4Core8ResponseINS3_17TrophyPackSummaryEEE", TK_METHOD_OK},
    {"EpYd-oklLoY", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_8Commerce10CategoriesEEC1Ev", TK_CTOR},
    {"FbtukaCjFCM", "_ZN3sce7Toolkit2NP2V28Commerce7Request22GetServiceEntitlementsD1Ev", TK_DTOR},
    {"GHipxIJRjZA", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_7Session8SessionsEE3getEv", TK_GET},
    {"H8cQuFAzgU4", "_ZN3sce7Toolkit2NP2V28Presence7Request11SetPresenceC1Ev", TK_CTOR},
    {"HeDIXZsguvA", "_ZN3sce7Toolkit2NP2V28Presence7Request11SetPresenceD1Ev", TK_DTOR},
    {"HvHPvwdkpqc", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_6Trophy16TrophyPackTrophyEEC1Ev", TK_CTOR},
    {"I25bsv++cMg", "_ZN3sce7Toolkit2NP2V28Commerce7Request29DisplayVoucherCodeInputDialogD1Ev", TK_DTOR},
    {"IcuECtP3L0E", "_ZN3sce7Toolkit2NP2V24Core4termERKNS3_7Request10TermParamsE", TK_METHOD_OK},
    {"ItZXngg8tfo", "_ZN3sce7Toolkit2NP2V26Trophy7Request18RegisterTrophyPackD1Ev", TK_DTOR},
    {"K3jkvDXyOHc", "_ZN3sce7Toolkit2NP2V26Trophy19getTrophyPackTrophyERKNS3_7Request19GetTrophyPackTrophyEPNS2_4Core8ResponseINS3_16TrophyPackTrophyEEE", TK_METHOD_OK},
    {"Kv8XXD7X7XQ", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_11UserProfile10NpProfilesEEC1Ev", TK_CTOR},
    {"LJAhfaa6+U4", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_7Session8SessionsEEC1Ev", TK_CTOR},
    {"LLdnqVnnNBQ", "_ZN3sce7Toolkit2NP2V24Core4initERKNS3_7Request10InitParamsE", TK_METHOD_OK},
    {"MF5Iqk2sIv8", "_ZN3sce7Toolkit2NP2V24Auth7Request11GetAuthCodeD1Ev", TK_DTOR},
    {"MkSYr+wZN-A", "_ZN3sce7Toolkit2NP2V24Auth7Request10GetIdTokenC1Ev", TK_CTOR},
    {"OZZoMxUSM+8", "_ZN3sce7Toolkit2NP2V28Presence11getPresenceERKNS3_7Request11GetPresenceEPNS2_4Core8ResponseINS3_8PresenceEEE", TK_METHOD_OK},
    {"P21OxFwKDac", "_ZN3sce7Toolkit2NP2V26Trophy7Request20GetTrophyPackSummaryD1Ev", TK_DTOR},
    {"PRGCDAuvO4U", "_ZN3sce7Toolkit2NP2V26Friend7Request15GetBlockedUsersD1Ev", TK_DTOR},
    {"QJWQZHC0y4Q", "_ZN3sce7Toolkit2NP2V26Trophy6unlockERKNS3_7Request6UnlockEPNS2_4Core8ResponseINS8_5EmptyEEE", TK_METHOD_OK},
    {"SLK65JLYrUE", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_8Presence8PresenceEEC1Ev", TK_CTOR},
    {"TK6fX5Lm-6A", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_6Trophy17TrophyPackSummaryEEC1Ev", TK_CTOR},
    {"UOVPAHM8ZHw", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_6Friend12BlockedUsersEEC1Ev", TK_CTOR},
    {"VFM4TzMTUPI", "_ZN3sce7Toolkit2NP2V28Commerce7Request11GetProductsD1Ev", TK_DTOR},
    {"VVWPUyolcGQ", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_6Friend7FriendsEED1Ev", TK_DTOR},
    {"W2D0GnkDQoY", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_6Trophy16UnlockedTrophiesEEC1Ev", TK_CTOR},
    {"XIbXIDTEUpo", "_ZN3sce7Toolkit2NP2V24Auth11getAuthCodeERKNS3_7Request11GetAuthCodeEPNS2_4Core8ResponseINS3_8AuthCodeEEE", TK_METHOD_OK},
    {"Y4+NqOIedOk", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_6Friend7FriendsEEC1Ev", TK_CTOR},
    {"YKkJF7Z7A34", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_8Commerce10CategoriesEE3getEv", TK_GET},
    {"YYs7Nb1n3rM", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_8Commerce8ProductsEED1Ev", TK_DTOR},
    {"YlpnKm99h3Q", "_ZN3sce7Toolkit2NP2V211UserProfile13getNpProfilesERKNS3_7Request13GetNpProfilesEPNS2_4Core8ResponseINS3_10NpProfilesEEE", TK_METHOD_OK},
    {"ZWEcg5Lqdfk", "_ZNK3sce7Toolkit2NP2V24Core12ResponseBase8getStateEv", TK_METHOD_OK},
    {"Zc34winWfMA", "_ZN3sce7Toolkit2NP2V28Presence7Request11GetPresenceC1Ev", TK_CTOR},
    {"a0+h8BwDUUA", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_8Commerce8ProductsEE3getEv", TK_GET},
    {"aA83TCJI9Kw", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_8Presence8PresenceEE3getEv", TK_GET},
    {"buMmdpEwRuM", "_ZN3sce7Toolkit2NP2V26Trophy18registerTrophyPackERKNS3_7Request18RegisterTrophyPackEPNS2_4Core8ResponseINS8_5EmptyEEE", TK_METHOD_OK},
    {"c6uPCbW4+lQ", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_8Presence8PresenceEED1Ev", TK_DTOR},
    {"cmvr0mOCX2Q", "_ZN3sce7Toolkit2NP2V27Session14sendInvitationERKNS3_7Request14SendInvitationEPNS2_4Core8ResponseINS8_5EmptyEEE", TK_METHOD_OK},
    {"dB9nauAWtgg", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS3_5EmptyEE5resetEv", TK_METHOD_OK},
    {"fh+OX5Gf-BA", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_4Auth8AuthCodeEE3getEv", TK_GET},
    {"gEoCCr0gCjQ", "_ZN3sce7Toolkit2NP2V26Friend7Request10GetFriendsD1Ev", TK_DTOR},
    {"hks1Z-mbivM", "_ZN3sce7Toolkit2NP2V28Commerce7Request22GetServiceEntitlementsC1Ev", TK_CTOR},
    {"hm52T5bPfJo", "_ZN3sce7Toolkit2NP2V28Commerce7Request21DisplayCheckoutDialogD1Ev", TK_DTOR},
    {"iPDEu4fu2jM", "_ZN3sce7Toolkit2NP2V26Trophy7Request20GetTrophyPackSummaryC1Ev", TK_CTOR},
    {"iaGdIXzjp80", "_ZN3sce7Toolkit2NP2V27Session7Request14SendInvitationD1Ev", TK_DTOR},
    {"iuLoJwrPubs", "_ZN3sce7Toolkit2NP2V27Session7Request6SearchD1Ev", TK_DTOR},
    {"kxzokJHZ2nY", "_ZN3sce7Toolkit2NP2V24Core12ResponseBase14getServerErrorEv", TK_METHOD_OK},
    {"kyyI+gLshKQ", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_7NpUtils12Notification15UserStateChangeEE3getEv", TK_GET},
    {"lx9ZDEcHdMY", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_7Session8SessionsEED1Ev", TK_DTOR},
    {"mIinAn8TvdY", "_ZN3sce7Toolkit2NP2V24Core12ResponseBase8isLockedEv", TK_METHOD_OK},
    {"mNNKaSxKjRU", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_4Auth8AuthCodeEED1Ev", TK_DTOR},
    {"mPorOjIR5M4", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_6Trophy17TrophyPackSummaryEE3getEv", TK_GET},
    {"mQf+iu0AG4M", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_6Friend12BlockedUsersEED1Ev", TK_DTOR},
    {"naSd3GcjHrA", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_6Trophy16TrophyPackTrophyEED1Ev", TK_DTOR},
    {"nrvHUJnoOGM", "_ZN3sce7Toolkit2NP2V28Commerce7Request11GetProductsC1Ev", TK_CTOR},
    {"o2PhQ4mUZuE", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_11UserProfile10NpProfilesEED1Ev", TK_DTOR},
    {"oBhiTEQfdoM", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_4Auth7IdTokenEE3getEv", TK_GET},
    {"omVg1AdJGYQ", "_ZN3sce7Toolkit2NP2V27Session7Request14SendInvitationC1Ev", TK_CTOR},
    {"owxYieafJSM", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_8Commerce19ServiceEntitlementsEED1Ev", TK_DTOR},
    {"p3j95bppH44", "_ZN3sce7Toolkit2NP2V27Session6searchERKNS3_7Request6SearchEPNS2_4Core8ResponseINS3_8SessionsEEE", TK_METHOD_OK},
    {"q-uIM1VNvwg", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_11UserProfile10NpProfilesEE3getEv", TK_GET},
    {"qCTiOEgKX3w", "_ZN3sce7Toolkit2NP2V28Presence7Request11GetPresenceD1Ev", TK_DTOR},
    {"qqkywbrhVtk", "_ZN3sce7Toolkit2NP2V26Friend7Request10GetFriendsC1Ev", TK_CTOR},
    {"rBCq22VDbTE", "_ZN3sce7Toolkit2NP2V28Commerce29displayVoucherCodeInputDialogERKNS3_7Request29DisplayVoucherCodeInputDialogEPNS2_4Core8ResponseINS8_5EmptyEEE", TK_METHOD_OK},
    {"raaLMARDhV0", "_ZN3sce7Toolkit2NP2V27Session7Request6SearchC1Ev", TK_CTOR},
    {"s0u30pOfi9Q", "_ZN3sce7Toolkit2NP2V24Core7Request10TermParamsC1Ev", TK_CTOR},
    {"t-tNMj1vbVw", "_ZN3sce7Toolkit2NP2V24Auth10getIdTokenERKNS3_7Request10GetIdTokenEPNS2_4Core8ResponseINS3_7IdTokenEEE", TK_METHOD_OK},
    {"t0eLBGKYT1E", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_6Trophy17TrophyPackSummaryEED1Ev", TK_DTOR},
    {"tlpXm5KLgqk", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS3_5EmptyEEC1Ev", TK_CTOR},
    {"txIF7w41SkM", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_6Trophy16TrophyPackTrophyEE3getEv", TK_GET},
    {"uS05QSvf+tY", "_ZN3sce7Toolkit2NP2V26Trophy7Request6UnlockC1Ev", TK_CTOR},
    {"v+jSLz4ul6w", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_6Trophy16TrophyPackTrophyEE5resetEv", TK_METHOD_OK},
    {"vUtyZPXF0X0", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_8Commerce8ProductsEE5resetEv", TK_METHOD_OK},
    {"vycUuYWwilI", "_ZN3sce7Toolkit2NP2V24Core7Request10TermParamsD1Ev", TK_DTOR},
    {"wkgY4lDEV9k", "_ZN3sce7Toolkit2NP2V26Trophy7Request18RegisterTrophyPackC1Ev", TK_CTOR},
    {"woB-r3hY2Jo", "_ZN3sce7Toolkit2NP2V28Commerce7Request13GetCategoriesD1Ev", TK_DTOR},
    {"xCg+igDBo3Q", "_ZN3sce7Toolkit2NP2V28Presence11setPresenceERKNS3_7Request11SetPresenceEPNS2_4Core8ResponseINS8_5EmptyEEE", TK_METHOD_OK},
    {"xotzrkLV-aU", "_ZN3sce7Toolkit2NP2V28Commerce11getProductsERKNS3_7Request11GetProductsEPNS2_4Core8ResponseINS3_8ProductsEEE", TK_METHOD_OK},
    {"ydaUojfUa70", "_ZN3sce7Toolkit2NP2V28Commerce21displayCheckoutDialogERKNS3_7Request21DisplayCheckoutDialogEPNS2_4Core8ResponseINS8_5EmptyEEE", TK_METHOD_OK},
    {"yl2jH6kx6VY", "_ZN3sce7Toolkit2NP2V24Core8ResponseINS2_8Commerce8ProductsEEC1Ev", TK_CTOR},
};

bool ps4_nptoolkit2_dispatch(GuestContext *ctx, const char *nid) {
    if (!ctx || !nid) return false;

    const NpToolkit2Entry *found = NULL;
    for (size_t i = 0; i < sizeof(s_toolkit_entries) / sizeof(s_toolkit_entries[0]); ++i) {
        if (strcmp(s_toolkit_entries[i].nid, nid) == 0) {
            found = &s_toolkit_entries[i];
            break;
        }
    }

    if (found) {
        switch (found->type) {
            case TK_CTOR:
                if (ctx->rdi && ctx->mem_base) {
                    memset((void *)(ctx->mem_base + ctx->rdi), 0, 256);
                }
                ctx->rax = ctx->rdi;
                break;
            case TK_DTOR:
            case TK_GET:
                ctx->rax = ctx->rdi;
                break;
            case TK_METHOD_OK:
            default:
                ctx->rax = 0;
                break;
        }
        ctx->rsp += 8;
        return true;
    }

    // Fallback for any other libSceNpToolkit2 function: return 0 / SCE_OK
    ctx->rax = 0;
    ctx->rsp += 8;
    return true;
}
