#include "lenses/ai/tracking/bytetrack-tracker.hpp"

#include <iostream>
#include <string>

namespace {

bool Expect(bool condition, const std::string &message)
{
	if (condition)
		return true;
	std::cerr << "FAIL: " << message << std::endl;
	return false;
}

lenses::core::MaskFrame MakeSingleInstanceFrame(uint64_t frame_id, int32_t class_id,
						float confidence, float x, float y,
						float width, float height)
{
	lenses::core::MaskFrame frame{};
	frame.frame_id = frame_id;
	frame.source_width = 1920;
	frame.source_height = 1080;
	frame.timestamp_ns = frame_id * 1000;

	lenses::core::MaskInstance instance{};
	instance.class_id = class_id;
	instance.confidence = confidence;
	instance.bbox_norm = {x, y, width, height};
	instance.timestamp_ns = frame.timestamp_ns;
	frame.instances.push_back(instance);
	return frame;
}

bool TestStableIdAcrossMotion()
{
	lenses::ai::tracking::ByteTrackTracker tracker;

	auto frame1 = MakeSingleInstanceFrame(1, 0, 0.92f, 0.10f, 0.10f, 0.20f, 0.30f);
	tracker.Update(frame1);
	if (!Expect(frame1.instances.size() == 1, "frame1 should contain one tracked instance"))
		return false;
	const uint64_t id1 = frame1.instances[0].track_id;
	if (!Expect(id1 != 0, "frame1 track id must be assigned"))
		return false;

	auto frame2 = MakeSingleInstanceFrame(2, 0, 0.93f, 0.12f, 0.10f, 0.20f, 0.30f);
	tracker.Update(frame2);
	if (!Expect(frame2.instances.size() == 1, "frame2 should contain one tracked instance"))
		return false;
	return Expect(frame2.instances[0].track_id == id1, "track id should stay stable across motion");
}

bool TestOcclusionBufferRetention()
{
	lenses::ai::tracking::ByteTrackConfig config{};
	config.track_buffer = 2;
	config.match_thresh = 0.2f;
	lenses::ai::tracking::ByteTrackTracker tracker(config);

	auto frame1 = MakeSingleInstanceFrame(1, 0, 0.95f, 0.30f, 0.20f, 0.20f, 0.20f);
	tracker.Update(frame1);
	if (!Expect(!frame1.instances.empty(), "frame1 should initialize track"))
		return false;
	const uint64_t id1 = frame1.instances[0].track_id;

	lenses::core::MaskFrame empty2{};
	empty2.frame_id = 2;
	empty2.source_width = 1920;
	empty2.source_height = 1080;
	(void)tracker.Update(empty2);

	auto frame3 = MakeSingleInstanceFrame(3, 0, 0.91f, 0.31f, 0.20f, 0.20f, 0.20f);
	tracker.Update(frame3);
	if (!Expect(!frame3.instances.empty(), "frame3 should contain one tracked instance"))
		return false;

	return Expect(frame3.instances[0].track_id == id1,
		      "track id should persist through short occlusion");
}

bool TestClassAwareAssociation()
{
	lenses::ai::tracking::ByteTrackConfig config{};
	config.class_aware = true;
	lenses::ai::tracking::ByteTrackTracker tracker(config);

	auto frame1 = MakeSingleInstanceFrame(1, 0, 0.95f, 0.45f, 0.30f, 0.20f, 0.20f);
	tracker.Update(frame1);
	if (!Expect(!frame1.instances.empty(), "frame1 should contain one tracked instance"))
		return false;
	const uint64_t id1 = frame1.instances[0].track_id;

	auto frame2 = MakeSingleInstanceFrame(2, 2, 0.96f, 0.45f, 0.30f, 0.20f, 0.20f);
	tracker.Update(frame2);
	if (!Expect(!frame2.instances.empty(), "frame2 should contain one tracked instance"))
		return false;
	return Expect(frame2.instances[0].track_id != id1,
		      "different class should not reuse previous class track id");
}

} // namespace

int main()
{
	bool ok = true;
	ok = TestStableIdAcrossMotion() && ok;
	ok = TestOcclusionBufferRetention() && ok;
	ok = TestClassAwareAssociation() && ok;

	if (!ok)
		return 1;

	std::cout << "bytetrack-tracker-test: PASS" << std::endl;
	return 0;
}
