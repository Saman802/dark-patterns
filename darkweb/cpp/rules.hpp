// rules.hpp - rule book: Rule metadata lives in a HashMap (O(1) lookup by rule id),
// keyword patterns feed the Aho-Corasick automaton.
#pragma once
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>
#include "hash_map.hpp"

namespace dw {

// Per-DOM-node flags (computed by the content script from computed styles)
enum Flag { F_HIDDEN = 1, F_CHECKED = 2, F_CHECKBOX = 4, F_BUTTON = 8, F_TINY = 16, F_LOWCONTRAST = 32, F_STRIKE = 64, F_RADIO = 128 };
// Context bits a keyword can raise on a node; they are OR-ed up the tree (bottom-up) for structural rules
enum Ctx { C_FREE_TRIAL = 1, C_RENEW = 2, C_SUBSCRIBE = 4, C_DECLINE = 8, C_CANCEL = 16, C_URGENT = 32 };

struct Rule {
    std::string id, category, explanation;
    double severity = 0;
    bool structural = false, custom = false;
};
struct PatternDef { std::string text, ruleId; int ctx; };

inline std::string normalizeText(const std::string& s) {
    std::string o; o.reserve(s.size());
    for (unsigned char c : s) {
        if (c >= 128 || c == '\t' || c == '\n' || c == '\r') o.push_back(' ');
        else if (c >= 'A' && c <= 'Z') o.push_back((char)(c + 32));
        else o.push_back((char)c);
    }
    return o;
}

class RuleBook {
public:
    HashMap<Rule> rules;
    HashMap<double> categoryWeight;
    std::vector<PatternDef> patterns;
    std::vector<std::string> ruleOrder;

    void addRule(const std::string& id, const std::string& cat, double sev, const std::string& why, bool structural = false, bool custom = false) {
        Rule r; r.id = id; r.category = cat; r.severity = sev; r.explanation = why; r.structural = structural; r.custom = custom;
        if (!rules.contains(id)) ruleOrder.push_back(id);
        rules.put(id, r);
        if (!categoryWeight.contains(cat)) categoryWeight.put(cat, 1.0);
    }
    void addPattern(const std::string& text, const std::string& ruleId, int ctx) {
        std::string t = normalizeText(text);
        if (!t.empty()) patterns.push_back({t, ruleId, ctx});
    }
    void add(const std::string& ruleId, int ctx, std::initializer_list<const char*> pats) {
        for (const char* p : pats) addPattern(p, ruleId, ctx);
    }

    void loadBuiltins() {
        categoryWeight.put("Urgency", 1.0);      categoryWeight.put("Scarcity", 1.0);
        categoryWeight.put("Social Proof", 0.8); categoryWeight.put("Confirmshaming", 1.1);
        categoryWeight.put("Exclusivity", 0.7);  categoryWeight.put("Nagging", 0.6);
        categoryWeight.put("Hidden Costs", 1.2); categoryWeight.put("Sneaking", 1.2);
        categoryWeight.put("Forced Continuity", 1.2); categoryWeight.put("Misdirection", 1.1);
        categoryWeight.put("Obstruction", 1.1);

        // ---- text rules (flag the node whose own text matches) ----
        addRule("T_URGENCY", "Urgency", 7, "Creates artificial time pressure so you decide before you can compare or think.");
        add("T_URGENCY", C_URGENT, {"hurry", "hurry up", "act now", "act fast", "limited time", "limited time offer", "offer ends", "deal ends",
            "ends in", "ends today", "ends tonight", "ends soon", "sale ends", "last chance", "don't miss out", "expires soon", "expires in",
            "today only", "flash sale", "before it's gone", "order within", "only today", "offer expires", "deal of the day", "countdown", "time is running out"});

        addRule("T_SCARCITY", "Scarcity", 7, "Claims stock is almost gone to rush the purchase; the claim is rarely verifiable.");
        add("T_SCARCITY", 0, {"only a few left", "only few left", "almost gone", "selling fast", "selling out fast", "running out", "low stock",
            "few left", "limited stock", "limited quantity", "while stocks last", "while supplies last", "last one", "stock is limited", "almost sold out", "nearly sold out"});
        for (int n = 1; n <= 9; ++n) {
            const char* suf[] = {"left", "remaining", "in stock", "items left", "units left", "room left", "rooms left", "seat left", "seats left", "tickets left"};
            for (const char* s : suf) addPattern("only " + std::to_string(n) + " " + s, "T_SCARCITY", 0);
            addPattern(std::to_string(n) + " left in stock", "T_SCARCITY", 0);
        }

        addRule("T_SOCIAL_PROOF", "Social Proof", 5, "Uses (often unverifiable) activity of other shoppers to pressure you into buying.");
        add("T_SOCIAL_PROOF", 0, {"people are viewing", "people are looking at this", "people viewing this", "bought in the last", "sold in the last",
            "sold in last", "in demand", "trending now", "others are viewing", "someone just bought", "recently purchased", "added to cart in the last",
            "in carts right now", "viewed in the last", "people bought this"});

        addRule("T_CONFIRMSHAMING", "Confirmshaming", 8, "The decline option is worded to make you feel foolish or guilty for saying no.");
        add("T_CONFIRMSHAMING", 0, {"no thanks, i don't want", "no thanks i hate saving", "i don't like saving money", "i prefer to pay full price",
            "i don't want to save", "no, i don't want", "no thanks, i'd rather", "i'll miss out", "i'm not interested in saving", "no, i like paying more",
            "i don't want free shipping", "i don't care about my", "no, i don't need", "i'd rather pay more", "i don't want to be healthy"});

        addRule("T_EXCLUSIVITY", "Exclusivity", 4, "Fake exclusivity makes an ordinary offer feel rare or personal to rush you.");
        add("T_EXCLUSIVITY", 0, {"exclusive deal", "exclusive offer", "members only", "just for you", "vip only", "invitation only", "secret deal", "special price just for you"});

        addRule("T_NAGGING", "Nagging", 3, "Repeatedly interrupts you to change a decision you already made.");
        add("T_NAGGING", 0, {"wait! before you go", "wait, don't go", "before you go", "are you sure you want to leave", "you're about to miss", "don't leave"});

        addRule("T_HIDDEN_COST", "Hidden Costs", 5, "Extra charges are mentioned only late or in passing, so the real price is higher than the headline price.");
        add("T_HIDDEN_COST", 0, {"plus applicable fees", "additional fees may apply", "fees may apply", "taxes and fees extra", "handling fee", "convenience fee",
            "processing fee", "service fee", "platform fee", "packaging fee", "surge fee", "booking fee", "additional charges", "charges may apply",
            "excluding taxes", "plus taxes", "shipping and handling", "resort fee", "delivery fee applies"});

        // ---- context keywords (no finding by themselves; they raise bits used by structural rules) ----
        add("", C_FREE_TRIAL, {"free trial", "try free", "start free", "first month free", "try for free", "free for 7 days", "free for 14 days",
            "free for 30 days", "7-day free", "14-day free", "30-day free", "7 day free", "30 day free", "free for a month"});
        add("", C_RENEW, {"auto-renew", "auto renew", "autorenew", "automatically renew", "renews automatically", "automatically charged", "will be charged",
            "recurring", "cancel anytime", "billed monthly", "billed annually", "billed yearly", "after the trial", "after your trial", "trial ends", "per month after", "/month after", "renews at"});
        add("", C_SUBSCRIBE, {"newsletter", "subscribe", "subscription", "membership", "insurance", "protection plan", "extended warranty", "donate", "donation",
            "premium", "marketing emails", "promotional", "partner offers", "offers and updates", "third parties", "add gift", "save my card", "prime"});
        add("", C_DECLINE, {"no thanks", "no thank you", "no, thanks", "skip", "maybe later", "not now", "i don't want", "decline", "continue without", "remind me later", "i'll pass"});
        add("", C_CANCEL, {"cancel subscription", "cancel membership", "unsubscribe", "cancel my account", "cancel plan", "stop subscription", "cancel auto-renew"});

        // ---- structural rules (decided from tree shape + flags + aggregated context bits) ----
        addRule("S_PRESELECTED_OPT_IN", "Sneaking", 8, "A checkbox for a subscription / add-on / marketing consent is already ticked, so doing nothing opts you in.", true);
        addRule("S_HIDDEN_PRESELECT", "Sneaking", 9, "A pre-ticked opt-in checkbox is hidden from view (tiny, off-screen or invisible) so you cannot notice or untick it.", true);
        addRule("S_MISDIRECTION", "Misdirection", 7, "The decline option is rendered tiny or faded next to a prominent accept button, steering you toward the sponsor's choice.", true);
        addRule("S_FORCED_CONTINUITY", "Forced Continuity", 8, "A free trial and automatic paid renewal are presented together; you may be charged after the trial unless you remember to cancel.", true);
        addRule("S_HIDDEN_CANCEL", "Obstruction", 7, "The cancel / unsubscribe control is deliberately hard to see (tiny or low-contrast) while sign-up is prominent.", true);
        addRule("S_COUNTDOWN", "Urgency", 6, "A running-clock style timer (HH:MM:SS) appears next to urgency wording; such timers frequently reset and are purely psychological.", true);
    }

    // Custom rule lines (tab separated):  id <TAB> category <TAB> severity <TAB> phrase
    void loadCustom(const char* tsv) {
        if (!tsv) return;
        std::string s(tsv); size_t pos = 0;
        while (pos < s.size()) {
            size_t e = s.find('\n', pos); if (e == std::string::npos) e = s.size();
            std::string line = s.substr(pos, e - pos); pos = e + 1;
            std::string f[4]; int k = 0; size_t a = 0;
            for (size_t i = 0; i <= line.size() && k < 4; ++i)
                if (i == line.size() || (line[i] == '\t' && k < 3)) { f[k++] = line.substr(a, i - a); a = i + 1; }
            if (k < 4 || f[3].empty() || f[0].empty()) continue;
            std::string rid = "C_" + f[0];
            double sev = atof(f[2].c_str()); if (sev < 1) sev = 1; if (sev > 10) sev = 10;
            if (!rules.contains(rid)) addRule(rid, f[1].empty() ? "Custom" : f[1], sev, "Custom rule: matches the phrase you configured for category '" + (f[1].empty() ? std::string("Custom") : f[1]) + "'.", false, true);
            addPattern(f[3], rid, 0);
        }
    }
};

}  // namespace dw
