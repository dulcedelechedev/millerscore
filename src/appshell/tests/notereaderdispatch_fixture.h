/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

// The production Note reader's final dispatch branches run unchanged, after
// note-specific tags have been excluded. Services record common-property and
// spanner calls; this is not the full XML parser or engraving object graph.
#include <QString>
#include <QVector>
#include <string_view>

namespace mu::engraving {
using AsciiStringView = std::string_view;
enum class ElementType { INVALID, GLISSANDO, TEXTLINE, SYMBOL };
enum class Pid { HIDE_GENERATED_PARENTHESES };
struct ReadContext {
    int conversions = 0;
    int unknownTypeLogs = 0;
    int commonReads = 0;
    int spannerReads = 0;
    int allocated = 0;
    int deleted = 0;
    void* dummy() { return this; }
};
struct XmlReader {
    std::string tag;
    bool readBool() { return true; }
};
struct Note {
    bool eidRead = false;
    bool visibleRead = false;
    bool overrideVisibility = false;
    void setOverrideBendVisibilityRules(bool value) { overrideVisibility = value; }
};
struct EngravingItem {
    ReadContext* context = nullptr;
    ElementType type = ElementType::INVALID;
    virtual ~EngravingItem() { ++context->deleted; }
    bool isSpanner() const { return type == ElementType::GLISSANDO || type == ElementType::TEXTLINE; }
};
struct Spanner : EngravingItem {
    enum class Anchor { NOTE, SEGMENT };
    Anchor anchor() const { return type == ElementType::GLISSANDO ? Anchor::NOTE : Anchor::SEGMENT; }
};
inline Spanner* toSpanner(EngravingItem* item) { return static_cast<Spanner*>(item); }
struct TConv {
    inline static ReadContext* context = nullptr;
    static ElementType fromXml(AsciiStringView tag, ElementType fallback) {
        ++context->conversions;
        if (tag == "Glissando") return ElementType::GLISSANDO;
        if (tag == "TextLine") return ElementType::TEXTLINE;
        if (tag == "Symbol") return ElementType::SYMBOL;
        ++context->unknownTypeLogs;
        return fallback;
    }
};
struct Factory {
    static EngravingItem* createItem(ElementType type, void* dummy) {
        auto* context = static_cast<ReadContext*>(dummy);
        ++context->allocated;
        if (type == ElementType::GLISSANDO || type == ElementType::TEXTLINE) {
            auto* spanner = new Spanner;
            spanner->context = context; spanner->type = type;
            return spanner;
        }
        auto* item = new EngravingItem;
        item->context = context; item->type = type;
        return item;
    }
};
struct TRead {
    static bool trailingNoteProperties(Note* n, XmlReader& e, ReadContext& ctx);
    static bool readItemProperties(Note* note, XmlReader& reader, ReadContext& context) {
        if (reader.tag == "eid") { note->eidRead = true; ++context.commonReads; return true; }
        if (reader.tag == "visible") { note->visibleRead = true; ++context.commonReads; return true; }
        return false;
    }
    static bool readProperty(Note*, AsciiStringView tag, XmlReader&, ReadContext& context, Pid) {
        if (tag != "hideGeneratedParentheses") return false;
        ++context.commonReads; return true;
    }
    static void readItem(Spanner* spanner, XmlReader&, ReadContext& context) {
        ++context.spannerReads; delete spanner;
    }
};
}
