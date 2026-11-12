#pragma once
#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-utils/i18n.h>

#include "core_engine.h"

namespace fcitx {

enum class ShanjieLayout { Standard, ETen };
FCITX_CONFIG_ENUM_NAME_WITH_I18N(ShanjieLayout, N_("Standard"), N_("ETen"));

enum class ShanjieStyle { Chat, Formal };
FCITX_CONFIG_ENUM_NAME_WITH_I18N(ShanjieStyle, N_("Chat"), N_("Formal"));

enum class ShanjieStrength { Conservative, Standard, Aggressive };
FCITX_CONFIG_ENUM_NAME_WITH_I18N(ShanjieStrength, N_("Conservative"), N_("Standard"), N_("Aggressive"));

enum class ShanjieKeyHabit { Shanjie, MicrosoftZhuyin };
FCITX_CONFIG_ENUM_NAME_WITH_I18N(ShanjieKeyHabit, N_("Shanjie"), N_("Microsoft Zhuyin"));

enum class ShanjieSelectionKeys { Digits, HomeRow, RestingKeys };
FCITX_CONFIG_ENUM_NAME_WITH_I18N(ShanjieSelectionKeys, N_("1 2 3 4 5 6 7 8 9"), N_("a s d f g h j k l"),
                                 N_("a s d f j k l ;"));

FCITX_CONFIGURATION(
    ShanjieConfig,
    OptionWithAnnotation<ShanjieLayout, ShanjieLayoutI18NAnnotation> layout{
        this, "Layout", _("Keyboard layout"), ShanjieLayout::Standard};
    OptionWithAnnotation<ShanjieStyle, ShanjieStyleI18NAnnotation> style{
        this, "Style", _("Writing style"), ShanjieStyle::Chat};
    OptionWithAnnotation<ShanjieKeyHabit, ShanjieKeyHabitI18NAnnotation> keyHabit{
        this, "KeyHabit", _("Key habit"), ShanjieKeyHabit::Shanjie};
    OptionWithAnnotation<ShanjieSelectionKeys, ShanjieSelectionKeysI18NAnnotation> selectionKeys{
        this, "SelectionKeys", _("Candidate selection keys"), ShanjieSelectionKeys::Digits};
    Option<bool> prediction{this, "Prediction", _("Show the prediction row"), true};
    Option<bool> abbreviation{this, "Abbreviation", _("Abbreviated input (initials only)"), false};
    Option<bool> demote{this, "Demote", _("Avoid ranking sensitive words first"), true};
    Option<bool> learning{this, "Learning", _("Learn from my choices"), true};
    Option<bool> rerank{this, "Rerank", _("Semantic correction"), true};
    OptionWithAnnotation<ShanjieStrength, ShanjieStrengthI18NAnnotation> rerankStrength{
        this, "RerankStrength", _("Semantic correction strength"), ShanjieStrength::Standard};
    Option<bool> english{this, "English", _("Mixed English input"), true};
    Option<bool> englishOverwrite{this, "EnglishOverwrite", _("English: a repeated column means English"), false};
    Option<bool> englishKeyOrder{this, "EnglishKeyOrder", _("English: judge by key order"), true};
    Option<bool> englishReversalGuard{
        this, "EnglishReversalGuard", _("English: reversed keys while typing fast stay Chinese"), true};
    Option<bool> fuzzyEnEng{this, "FuzzyEnEng", _("Fuzzy: \xe3\x84\xa3 / \xe3\x84\xa5"), false};
    Option<bool> fuzzyZhZ{this, "FuzzyZhZ", _("Fuzzy: \xe3\x84\x93 / \xe3\x84\x97"), false};
    Option<bool> fuzzyChC{this, "FuzzyChC", _("Fuzzy: \xe3\x84\x94 / \xe3\x84\x98"), false};
    Option<bool> fuzzyShS{this, "FuzzyShS", _("Fuzzy: \xe3\x84\x95 / \xe3\x84\x99"), false};
    Option<bool> fuzzyAdjacent{this, "FuzzyAdjacent", _("Fuzzy: adjacent-key slips"), false};
    Option<bool> packAcg{this, "PackAcg", _("Word pack: anime and games"), true};
    Option<bool> packQuotes{this, "PackQuotes", _("Word pack: classic quotes"), false};
    Option<bool> packIt{this, "PackIt", _("Word pack: IT and AI"), true};
    Option<bool> packMeme{this, "PackMeme", _("Word pack: memes and livestream slang"), true};
    Option<bool> packBeauty{this, "PackBeauty", _("Word pack: beauty and skincare"), true};);

inline shanjie::Config toCoreConfig(const ShanjieConfig &c) {
    shanjie::Config out;
    out.layout = *c.layout == ShanjieLayout::ETen ? 1 : 0;
    out.packs = (*c.packAcg ? SHANJIE_PACK_ACG : 0u) | (*c.packQuotes ? SHANJIE_PACK_QUOTES : 0u) |
                (*c.packIt ? SHANJIE_PACK_IT : 0u) | (*c.packMeme ? SHANJIE_PACK_MEME : 0u) |
                (*c.packBeauty ? SHANJIE_PACK_BEAUTY : 0u);
    out.profile = *c.style == ShanjieStyle::Formal ? 1 : 0;
    out.demote = *c.demote;
    out.prediction = *c.prediction;
    out.abbreviation = *c.abbreviation;
    out.english = *c.english;
    out.englishOverwrite = *c.englishOverwrite;
    out.englishKeyOrder = *c.englishKeyOrder;
    out.englishReversalGuard = *c.englishReversalGuard;
    out.fuzzy = (*c.fuzzyEnEng ? 1u : 0u) | (*c.fuzzyZhZ ? 2u : 0u) | (*c.fuzzyChC ? 4u : 0u) |
                (*c.fuzzyShS ? 8u : 0u) | (*c.fuzzyAdjacent ? 16u : 0u);
    out.rerank = *c.rerank;
    out.rerankLevel = static_cast<uint32_t>(*c.rerankStrength);
    out.keyScheme = *c.keyHabit == ShanjieKeyHabit::MicrosoftZhuyin ? 1 : 0;
    out.learning = *c.learning;
    out.selectionKeys = static_cast<uint32_t>(*c.selectionKeys);
    return out;
}

} // namespace fcitx
