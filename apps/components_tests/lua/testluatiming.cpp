#include <gtest/gtest.h>

#include <components/lua/luatiming.hpp>

#include <string>

namespace
{
    using LuaUtil::LuaTiming;
    using LuaUtil::ScopedLuaTiming;
    using LuaUtil::ScopedLuaTimingLabel;

    std::string name(int i)
    {
        return "s" + std::to_string(i);
    }

    struct LuaTimingTest : ::testing::Test
    {
        void SetUp() override
        {
            LuaTiming::sEnabled = false;
            LuaTiming::reset();
        }
        void TearDown() override { LuaTiming::sEnabled = false; }
    };

    TEST_F(LuaTimingTest, AddAndFormat)
    {
        LuaTiming::add(3, "onUpdate", 0.002);
        LuaTiming::add(3, "onUpdate", 0.002);
        LuaTiming::add(5, "onFrame", 0.001);
        LuaTiming::addFrame();
        LuaTiming::addFrame();
        auto lines = LuaTiming::formatAndReset(name);
        ASSERT_EQ(lines.size(), 3u);
        EXPECT_EQ(lines[0], "NMLUA|frames=2");
        EXPECT_EQ(lines[1], "NMLUA|s3|onUpdate|calls=2|ms_total=4.000|ms_per_frame=2.000|ms_max=2.000");
        EXPECT_EQ(lines[2], "NMLUA|s5|onFrame|calls=1|ms_total=1.000|ms_per_frame=0.500|ms_max=1.000");
    }

    TEST_F(LuaTimingTest, ResetClears)
    {
        LuaTiming::add(1, "onUpdate", 0.001);
        LuaTiming::addFrame();
        LuaTiming::formatAndReset(name);
        auto lines = LuaTiming::formatAndReset(name);
        ASSERT_EQ(lines.size(), 1u);
        EXPECT_EQ(lines[0], "NMLUA|frames=0");
    }

    TEST_F(LuaTimingTest, MaxEntries)
    {
        for (int i = 0; i < 50; ++i)
            LuaTiming::add(i, "onUpdate", 0.001 * (i + 1));
        auto lines = LuaTiming::formatAndReset(name, 40);
        ASSERT_EQ(lines.size(), 41u);
        EXPECT_EQ(lines[1].rfind("NMLUA|s49|", 0), 0u);
    }

    TEST_F(LuaTimingTest, NestedCountsOnlyOuter)
    {
        LuaTiming::sEnabled = true;
        {
            ScopedLuaTimingLabel label("onUpdate");
            ScopedLuaTiming a(1);
            {
                ScopedLuaTiming b(2);
            }
        }
        auto lines = LuaTiming::formatAndReset(name);
        ASSERT_EQ(lines.size(), 2u);
        EXPECT_EQ(lines[1].rfind("NMLUA|s1|onUpdate|calls=1|", 0), 0u);
    }

    TEST_F(LuaTimingTest, DisabledRecordsNothing)
    {
        {
            ScopedLuaTiming a(1);
        }
        auto lines = LuaTiming::formatAndReset(name);
        ASSERT_EQ(lines.size(), 1u);
        EXPECT_EQ(lines[0], "NMLUA|frames=0");
    }

    TEST_F(LuaTimingTest, DefaultLabelIsCallback)
    {
        LuaTiming::sEnabled = true;
        {
            ScopedLuaTiming a(7);
        }
        auto lines = LuaTiming::formatAndReset(name);
        ASSERT_EQ(lines.size(), 2u);
        EXPECT_NE(lines[1].find("|callback|"), std::string::npos);
    }
}
