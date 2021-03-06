#include "drake/geometry/render/render_engine.h"

#include <map>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "drake/common/drake_optional.h"
#include "drake/common/test_utilities/eigen_matrix_compare.h"
#include "drake/common/test_utilities/expect_throws_message.h"
#include "drake/geometry/test_utilities/dummy_render_engine.h"

namespace drake {
namespace geometry {
namespace render {

using math::RigidTransformd;

class RenderEngineTester {
 public:
  explicit RenderEngineTester(const RenderEngine* engine) : engine_(*engine) {}

  const std::unordered_map<RenderIndex, GeometryIndex>& update_map() const {
    return engine_.update_indices_;
  }

  const std::unordered_map<RenderIndex, GeometryIndex>& anchored_map() const {
    return engine_.anchored_indices_;
  }

 private:
  const RenderEngine& engine_;
};

namespace {

using geometry::internal::DummyRenderEngine;
using systems::sensors::ColorI;
using systems::sensors::ColorD;

// Tests the RenderEngine-specific functionality for managing registration of
// geometry and its corresponding update behavior. The former should configure
// each geometry correctly on whether it gets updated or not, and the latter
// will confirm that the right geometries get updated.
GTEST_TEST(RenderEngine, RegistrationAndUpdate) {
  // Change the default render label to something registerable.
  DummyRenderEngine engine({RenderLabel::kDontCare});

  // Configure parameters for registering visuals.
  PerceptionProperties skip_properties = engine.rejecting_properties();
  PerceptionProperties add_properties = engine.accepting_properties();
  Sphere sphere(1.0);
  RigidTransformd X_WG = RigidTransformd::Identity();
  // A collection of poses to provide to calls to UpdatePoses(). Configured
  // to all identity transforms because the values generally don't matter. In
  // the single case where it does matter, a value is explicitly set (see
  // below).
  std::vector<RigidTransformd> X_WG_all{3, X_WG};

  // These test cases are accumulative; re-ordering them will require
  // refactoring.

  // Tests that rely on the RenderEngine's default value are tested below in the
  // DefaultRenderLabel test.

  // Create properties with the given RenderLabel value.
  auto make_properties = [](RenderLabel label) {
    PerceptionProperties properties;
    properties.AddProperty("label", "id", label);
    return properties;
  };

  // Case: Explicitly providing the unspecified or empty render label throws.
  for (const auto label : {RenderLabel::kEmpty, RenderLabel::kUnspecified}) {
    DRAKE_EXPECT_THROWS_MESSAGE(
        engine.RegisterVisual(GeometryIndex(0), sphere,
                              make_properties(label), X_WG,
                              false),
        std::logic_error,
        "Cannot register a geometry with the 'unspecified' or 'empty' render "
        "labels.*");
  }

  // Case: the shape is configured to be ignored by the render engine. Returns
  // nullopt (and other arguments do not matter).
  optional<RenderIndex> optional_index = engine.RegisterVisual(
      GeometryIndex(0), sphere, skip_properties, X_WG, false);
  EXPECT_FALSE(optional_index);
  optional_index = engine.RegisterVisual(GeometryIndex(0), sphere,
                                         skip_properties, X_WG, true);
  EXPECT_FALSE(optional_index);
  // Confirm nothing is updated - because nothing is registered.
  engine.UpdatePoses(X_WG_all);
  EXPECT_EQ(engine.updated_indices().size(), 0);

  // Case: the shape is configured for registration, but does *not* require
  // updating. We get a valid render index, but it is _not_ included in
  // UpdatePoses().
  optional_index = engine.RegisterVisual(GeometryIndex(1), sphere,
                                         add_properties, X_WG, false);
  EXPECT_TRUE(optional_index);
  engine.UpdatePoses(X_WG_all);
  EXPECT_EQ(engine.updated_indices().size(), 0);

  // Case: the shape is configured for registration *and* requires updating. We
  // get a valid render index and it _is_ included in UpdatePoses().
  GeometryIndex update_index{2};
  // Configure the pose for index 2 to *not* be the identity so we can confirm
  // that the registered GeometryIndex is properly associated with the resulting
  // RenderIndex.
  const Vector3<double> p_WG(1, 2, 3);
  X_WG_all[update_index].set_translation(p_WG);
  optional_index =
      engine.RegisterVisual(update_index, sphere, add_properties, X_WG, true);
  EXPECT_TRUE(optional_index);
  engine.UpdatePoses(X_WG_all);
  EXPECT_EQ(engine.updated_indices().size(), 1);
  ASSERT_EQ(engine.updated_indices().count(*optional_index), 1);
  EXPECT_TRUE(CompareMatrices(
      engine.updated_indices().at(*optional_index).translation(), p_WG));
}

// Tests the removal of geometry from the renderer -- confirms that the
// RenderEngine is
//   a) Reporting the correct geometry index for the removed geometry and
//   b) Updating the remaining RenderIndex -> GeometryIndex pairs correctly.
GTEST_TEST(RenderEngine, RemoveGeometry) {
  const int need_update_count = 3;
  const int anchored_count = 2;
  std::vector<GeometryIndex> render_index_to_geometry_index;
  // Configure a clean render engine so each test is independent. Specifically,
  // It creates three dynamic geometries and two anchored. The initial render
  // index and geometry index matches for each geometry. Conceptually, we'll
  // have two maps:
  //  dynamic map: {{0, 0}, {1, 1}, {2, 2}}
  //  anchored map: {{3, 3}, {4, 4}}
  // Ultimately, we'll examine the the maps after removing geometry to confirm
  // the state of the mappings as a perturbation from this initial condition.
  auto make_engine = [&render_index_to_geometry_index]() -> DummyRenderEngine {
    render_index_to_geometry_index.clear();
    // Change the default render label to something registerable.
    DummyRenderEngine engine({RenderLabel::kDontCare});
    // A set of properties that will cause a shape to be properly registered.
    PerceptionProperties add_properties = engine.accepting_properties();
    RigidTransformd X_WG = RigidTransformd::Identity();
    Sphere sphere(1.0);

    for (int i = 0; i < need_update_count + anchored_count; ++i) {
      const GeometryIndex geometry_index = GeometryIndex(i);
      optional<RenderIndex> render_index = engine.RegisterVisual(
          geometry_index, sphere, add_properties, X_WG, i < need_update_count);
      if (!render_index || *render_index != i) {
        throw std::logic_error("Unexpected render indices");
      }
      render_index_to_geometry_index.push_back(geometry_index);
    }
    return engine;
  };

  // Function for performing the removal and testing the results.
  auto expect_removal =
      [make_engine](RenderIndex remove_index, optional<RenderIndex> move_index,
         optional<GeometryIndex> expected_moved,
         std::initializer_list<std::pair<int, int>> dynamic_map,
         std::initializer_list<std::pair<int, int>> anchored_map,
         const char* case_description) {
        DummyRenderEngine engine = make_engine();
        engine.set_moved_index(move_index);
        optional<GeometryIndex> moved_index =
            engine.RemoveGeometry(remove_index);
        EXPECT_EQ(moved_index, expected_moved) << case_description;
        RenderEngineTester tester(&engine);
        EXPECT_EQ(tester.update_map().size(), dynamic_map.size())
            << case_description;
        for (const auto& pair : dynamic_map) {
          EXPECT_EQ(tester.update_map().at(RenderIndex(pair.first)),
                    GeometryIndex(pair.second))
              << case_description;
        }
        EXPECT_EQ(tester.anchored_map().size(), anchored_map.size())
            << case_description;
        for (const auto& pair : anchored_map) {
          EXPECT_EQ(tester.anchored_map().at(RenderIndex(pair.first)),
                    GeometryIndex(pair.second))
              << case_description;
        }
      };

  using IndexMap = std::initializer_list<std::pair<int, int>>;

  // Case 1: remove dynamic geometry where nothing gets moved. Specifically,
  // remove the geometry (2, 2) with nothing else changing.
  {
    const RenderIndex remove_index(need_update_count - 1);
    optional<RenderIndex> moved_render_index = nullopt;
    optional<GeometryIndex> moved_geometry_index = nullopt;
    // Note: loss of need_update_count - 1 (i.e., 2) from the dynamic map.
    IndexMap expected_dynamic_map = {{0, 0}, {1, 1}};
    IndexMap expected_anchored_map = {{3, 3}, {4, 4}};
    expect_removal(remove_index, moved_render_index, moved_geometry_index,
                   expected_dynamic_map, expected_anchored_map, "case 1");
  }

  // Case 2: remove dynamic geometry where dynamic geometry gets moved.
  // Specifically, we have three dynamic geometries (0, 1, 2). We remove
  // geometry 1 and 2 gets moved into its slot.
  {
    const RenderIndex remove_index(1);
    const int move_value = 2;
    optional<RenderIndex> moved_render_index = RenderIndex(move_value);
    optional<GeometryIndex> moved_geometry_index = GeometryIndex(move_value);
    // Note: loss (1, 1) and (2, 2) gets moved to (1, 2) in the dynamic map.
    IndexMap expected_dynamic_map = {{0, 0}, {1, 2}};
    IndexMap expected_anchored_map = {{3, 3}, {4, 4}};
    expect_removal(remove_index, moved_render_index, moved_geometry_index,
                   expected_dynamic_map, expected_anchored_map, "case 2");
  }

  // Case 3: remove dynamic geometry where anchored geometry gets moved.
  // Specifically, remove the last dynamic geometry (2, 2), and move the last
  // anchored geometry (4, 4) into its slot.
  {
    const RenderIndex remove_index(2);
    const int move_value = 4;
    optional<RenderIndex> moved_render_index = RenderIndex(move_value);
    optional<GeometryIndex> moved_geometry_index = GeometryIndex(move_value);
    // Note: loss of (2, 2) from the dynamic map.
    IndexMap expected_dynamic_map = {{0, 0}, {1, 1}};
    // Note: (4, 4) gets moved to use the removed render index (2). So,
    // (4, 4) becomes (2, 4).
    IndexMap expected_anchored_map = {{3, 3}, {2, 4}};
    expect_removal(remove_index, moved_render_index, moved_geometry_index,
                   expected_dynamic_map, expected_anchored_map, "case 3");
  }

  // Case 4: remove anchored geometry where nothing gets moved.
  {
    const RenderIndex remove_index(4);
    optional<RenderIndex> moved_render_index = nullopt;
    optional<GeometryIndex> moved_geometry_index = nullopt;
    // Dynamic map untouched.
    IndexMap expected_dynamic_map = {{0, 0}, {1, 1}, {2, 2}};
    // Geometry (4, 4) has been removed.
    IndexMap expected_anchored_map = {{3, 3}};
    expect_removal(remove_index, moved_render_index, moved_geometry_index,
                   expected_dynamic_map, expected_anchored_map, "case 4");
  }

  // Case 5: remove anchored geometry where dynamic geometry gets moved.
  // Specifically, remove the _last_ anchored geometry (4, 4) and move the
  // last dynamic geometry (2, 2) into that slot to become (2, 4).
  {
    const RenderIndex remove_index(4);
    const int move_value = 2;
    optional<RenderIndex> moved_render_index = RenderIndex(move_value);
    optional<GeometryIndex> moved_geometry_index = GeometryIndex(move_value);
    // Note: (2, 2) has been moved into the remove index (4, 2).
    IndexMap expected_dynamic_map = {{0, 0}, {1, 1}, {4, 2}};
    // Note: (4, 4) has been removed
    IndexMap expected_anchored_map = {{3, 3}};
    expect_removal(remove_index, moved_render_index, moved_geometry_index,
                   expected_dynamic_map, expected_anchored_map, "case 5");
  }

  // Case 6: remove anchored geometry where anchored geometry gets moved.
  // We have two anchored geometries: (3, 3) and (4, 4). Remove (3, 3) and move
  // (4, 4) into its place (3, 4).
  {
    const RenderIndex remove_index(3);
    const int move_value = 4;
    optional<RenderIndex> moved_render_index = RenderIndex(move_value);
    optional<GeometryIndex> moved_geometry_index = GeometryIndex(move_value);
    // Note: Dynamic is untouched.
    IndexMap expected_dynamic_map = {{0, 0}, {1, 1}, {2, 2}};
    // Note: Remove (3, 3) and move (4, 4) into position 3: (3, 4).
    IndexMap expected_anchored_map = {{3, 4}};
    expect_removal(remove_index, moved_render_index, moved_geometry_index,
                   expected_dynamic_map, expected_anchored_map, "case 6");
  }
}

GTEST_TEST(RenderEngine, ColorLabelConversion) {
  // Explicitly testing labels at *both* ends of the reserved space -- this
  // assumes that the reserved labels are at the top end; if that changes, we'll
  // need a different mechanism to get a large-valued label.
  RenderLabel label1 = RenderLabel(0);
  RenderLabel label2 = RenderLabel(RenderLabel::kMaxUnreserved - 1);
  RenderLabel label3 = RenderLabel::kEmpty;

  // A ColorI should be invertible back to the original label.
  ColorI color1 = DummyRenderEngine::GetColorIFromLabel(label1);
  ColorI color2 = DummyRenderEngine::GetColorIFromLabel(label2);
  ColorI color3 = DummyRenderEngine::GetColorIFromLabel(label3);
  EXPECT_EQ(label1, DummyRenderEngine::LabelFromColor(color1));
  EXPECT_EQ(label2, DummyRenderEngine::LabelFromColor(color2));
  EXPECT_EQ(label3, DummyRenderEngine::LabelFromColor(color3));

  // Different labels should produce different colors.
  ASSERT_NE(label1, label2);
  ASSERT_NE(label2, label3);
  ASSERT_NE(label1, label3);
  auto same_colors = [](const auto& expected, const auto& test) {
    if (expected.r != test.r || expected.g != test.g || expected.b != test.b) {
      return ::testing::AssertionFailure()
          << "Expected color " << expected << ", found " << test;
    }
    return ::testing::AssertionSuccess();
  };

  EXPECT_FALSE(same_colors(color1, color2));
  EXPECT_FALSE(same_colors(color2, color3));
  EXPECT_FALSE(same_colors(color1, color3));

  // Different labels should also produce different Normalized colors.
  ColorD color1_d = DummyRenderEngine::GetColorDFromLabel(label1);
  ColorD color2_d = DummyRenderEngine::GetColorDFromLabel(label2);
  ColorD color3_d = DummyRenderEngine::GetColorDFromLabel(label3);
  EXPECT_FALSE(same_colors(color1_d, color2_d));
  EXPECT_FALSE(same_colors(color1_d, color3_d));
  EXPECT_FALSE(same_colors(color2_d, color3_d));

  // The normalized color should simply be the integer color divided by 255.
  ColorD color1_d_by_hand{color1.r / 255., color1.g / 255., color1.b / 255.};
  ColorD color2_d_by_hand{color2.r / 255., color2.g / 255., color2.b / 255.};
  ColorD color3_d_by_hand{color3.r / 255., color3.g / 255., color3.b / 255.};

  EXPECT_TRUE(same_colors(color1_d, color1_d_by_hand));
  EXPECT_TRUE(same_colors(color2_d, color2_d_by_hand));
  EXPECT_TRUE(same_colors(color3_d, color3_d_by_hand));
}

// Tests the documented behavior for configuring the default render label.
GTEST_TEST(RenderEngine, DefaultRenderLabel) {
  // Case: Confirm RenderEngine default is kUnspecified.
  {
    DummyRenderEngine engine;
    EXPECT_EQ(engine.default_render_label(), RenderLabel::kUnspecified);
  }

  // Case: Confirm kDontCare is valid.
  {
    DummyRenderEngine engine(RenderLabel::kDontCare);
    EXPECT_EQ(engine.default_render_label(), RenderLabel::kDontCare);
  }

  // Case: Confirm construction with alternate label is forbidden.
  {
    for (auto label :
         {RenderLabel::kDoNotRender, RenderLabel::kEmpty, RenderLabel{10}}) {
      DRAKE_EXPECT_THROWS_MESSAGE(DummyRenderEngine{label}, std::logic_error,
                                  ".* default render label must be either "
                                  "'kUnspecified' or 'kDontCare'");
    }
  }
}

}  // namespace
}  // namespace render
}  // namespace geometry
}  // namespace drake
