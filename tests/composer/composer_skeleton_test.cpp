#include <gtest/gtest.h>

#include "composer/composer.h"

namespace bach::composer {

TEST(ComposerSkeletonTest, LibraryLinks) {
  EXPECT_TRUE(isComposerLibLinked());
}

}  // namespace bach::composer
