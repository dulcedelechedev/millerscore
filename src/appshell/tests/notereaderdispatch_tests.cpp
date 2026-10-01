/* SPDX-License-Identifier: GPL-3.0-only */
#include <gtest/gtest.h>
#include "notereaderdispatch_fixture.h"

using namespace mu::engraving;

TEST(NoteReaderDispatchTests, CommonNoteIdentityAndVisibilityDoNotProbeElementTypes)
{
    for (const std::string& tag : { "eid", "visible" }) {
        ReadContext context; TConv::context = &context;
        Note note; XmlReader reader { tag };
        EXPECT_TRUE(TRead::trailingNoteProperties(&note, reader, context));
        EXPECT_EQ(context.commonReads, 1);
        EXPECT_EQ(context.conversions, 0) << tag;
        EXPECT_EQ(context.unknownTypeLogs, 0) << tag;
        EXPECT_TRUE(tag == "eid" ? note.eidRead : note.visibleRead);
        EXPECT_EQ(context.allocated, 0);
    }
}

TEST(NoteReaderDispatchTests, NoteAnchoredSpannersStillReadAndOtherAnchorsRejectWithoutLeaks)
{
    for (const std::string& tag : { "Glissando", "TextLine", "Symbol" }) {
        ReadContext context; TConv::context = &context;
        Note note; XmlReader reader { tag };
        const bool expected = tag == "Glissando";
        EXPECT_EQ(TRead::trailingNoteProperties(&note, reader, context), expected) << tag;
        EXPECT_EQ(context.commonReads, 0);
        EXPECT_EQ(context.conversions, 1);
        EXPECT_EQ(context.unknownTypeLogs, 0);
        EXPECT_EQ(context.spannerReads, expected ? 1 : 0);
        EXPECT_EQ(context.allocated, 1);
        EXPECT_EQ(context.deleted, 1);
    }
}

TEST(NoteReaderDispatchTests, UnknownTagsRemainRejectedAndKnownVisibilityPropertyRemainsReadable)
{
    ReadContext context; TConv::context = &context;
    Note note; XmlReader unknown { "unknownNoteTag" };
    EXPECT_FALSE(TRead::trailingNoteProperties(&note, unknown, context));
    EXPECT_EQ(context.unknownTypeLogs, 1);
    XmlReader overrideProperty { "overrideBendVisibilityRules" };
    EXPECT_TRUE(TRead::trailingNoteProperties(&note, overrideProperty, context));
    EXPECT_TRUE(note.overrideVisibility);
    XmlReader parentheses { "hideGeneratedParentheses" };
    EXPECT_TRUE(TRead::trailingNoteProperties(&note, parentheses, context));
    EXPECT_EQ(context.commonReads, 1);
}
