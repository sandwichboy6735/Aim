// Aim analysis with no engine dependencies, so it can be tested outside Unreal.
// Positions can be in any unit (Unreal uses cm). Angles are in degrees, times in seconds.
// Directions use Unreal's axes: X forward, Y right, Z up.
#pragma once

#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace aimcore
{
	struct Vec3
	{
		double x = 0.0;
		double y = 0.0;
		double z = 0.0;
	};

	enum class Kind : int
	{
		Snap = 0,
		Reaction,
		Robotic,
		Wall,
		Accuracy,
		Count
	};

	const char* kindName(Kind kind);

	struct TargetSample
	{
		int id = 0;
		Vec3 aimPoint;        // where a perfect aimer would point (the head), rewound for the shooter's ping
		double radius = 12.0; // size of the aim point, in the same unit as positions
		bool visible = false; // line of sight from the shooter's eye to the aim point
		bool moving = false;  // the target itself is moving
	};

	struct Detection
	{
		Kind kind = Kind::Snap;
		double time = 0.0;
		std::string detail;
	};

	struct Config
	{
		// Aim counts as "on target" within this many head radii of the aim point.
		double onTargetRadii = 3.0;

		// Snap: the aim was nearly still (no update in the previous snapLeadInSeconds turned more than
		// snapLeadInFraction of the jump), then one update turns at least snapMinDeg and lands within
		// snapCenterRadii head radii of a target, and a shot comes within snapFireWindow seconds after
		// (or snapShotBeforeWindow before, when the shot reaches the server ahead of the aim update).
		// A human flick spans several updates, so it fails the "still, then one big turn" test, but only
		// when updates are frequent: the check runs only while they arrive at least every
		// snapMaxUpdateGap seconds (40 Hz). Slower than that, a fast flick fits between two updates and
		// looks exactly like a snap. The reaction-time check still catches snap bots at lower rates.
		double snapMaxUpdateGap = 0.025;
		double snapMinDeg = 12.0;
		double snapLeadInSeconds = 0.07;
		double snapLeadInFraction = 0.1;
		double snapCenterRadii = 0.25;
		double snapFireWindow = 0.15;
		double snapShotBeforeWindow = 0.1;

		// A target that stops being reported (killed, destroyed) is still checked for snaps and reaction
		// time for this long, because a one-shot kill removes it before the aim update that killed it arrives.
		double lostTargetSeconds = 0.3;

		// Inhuman reaction: on target less than reactionMaxSeconds after the target came into view
		// while the aim was at least reactionMinStartDeg away from it.
		double reactionMaxSeconds = 0.14;
		double reactionMinStartDeg = 10.0;

		// Robotic tracking: on a moving target for roboticWindow seconds while the aim error wobbles
		// (standard deviation) by less than roboticWobbleRadii head radii.
		double roboticWindow = 0.6;
		double roboticWobbleRadii = 0.1;

		// Through walls: aim stays within a tolerance of a hidden, moving target for wallMinSeconds while
		// the target crosses at least wallMinSweepDeg of view and wallSweepPerTolerance tolerances.
		// The tolerance is wallToleranceRadii head radii, and at least wallMinToleranceDeg.
		// A crosshair held still on a spot only sees a target cross about 2 tolerances (in one side, out
		// the other), so the sweep rule means the aim has to move with the target.
		double wallToleranceRadii = 2.0;
		double wallMinToleranceDeg = 1.0;
		double wallMinSeconds = 0.8;
		double wallMinSweepDeg = 5.0;
		double wallSweepPerTolerance = 3.0;

		// Impossible accuracy: at least accuracyThreshold of the last accuracyWindow shots hit.
		int accuracyWindow = 20;
		int accuracyMinShots = 15;
		double accuracyThreshold = 0.9;

		// Suspicion added per detection (indexed by Kind), and how fast it cools off.
		double weights[static_cast<int>(Kind::Count)] = { 18.0, 12.0, 15.0, 25.0, 20.0 };
		double decayPerSecond = 4.0;
		double maxSuspicion = 100.0;
	};

	class Analyzer
	{
	public:
		explicit Analyzer(const Config& initialConfig = Config());

		// Call once per tick with the shooter's eye point, aim direction and every enemy.
		void addSample(double now, const Vec3& eye, const Vec3& aimDirection, const std::vector<TargetSample>& targets);

		// Call each time the shooter fires.
		void addShot(double now, bool hit);

		void reset();
		double suspicion() const { return score; }
		int count(Kind kind) const { return counts[static_cast<int>(kind)]; }

		// Detections since the last call.
		std::vector<Detection> takeDetections();

		Config config;

	private:
		struct TrackPoint
		{
			double time;
			double yawError;
			double pitchError;
			double radiusDeg;
		};

		struct TurnPoint
		{
			double time;
			double degrees;
		};

		struct TargetState
		{
			bool on = false;
			bool wasVisible = false;
			double visibleSince = -1.0;
			double errorWhenSeen = 0.0;
			std::deque<TrackPoint> track;
			double lastRobotic = -1e9;
			double hiddenFollow = 0.0;
			double hiddenSweep = 0.0;
			bool hasLastDirection = false;
			Vec3 lastDirection;
			double lastWall = -1e9;
			double lastSample = 0.0;
			Vec3 lastAimPoint;
			double lastRadius = 0.0;
		};

		void flag(Kind kind, double now, const std::string& detail);
		void checkReaction(TargetState& state, double now, bool on);

		double score = 0.0;
		int counts[static_cast<int>(Kind::Count)] = {};
		std::vector<Detection> pending;
		std::unordered_map<int, TargetState> states;
		bool hasPrevious = false;
		double previousTime = 0.0;
		Vec3 previousDirection;
		std::deque<TurnPoint> recentTurns;
		bool snapArmed = false;
		double snapTime = 0.0;
		double snapTurn = 0.0;
		double lastShotTime = -1e9;
		std::deque<bool> shots;
		double lastAccuracyFlag = -1e9;
	};
}
