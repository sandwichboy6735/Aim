#include "AimAnalyzerCore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace aimcore
{
	namespace
	{
		constexpr double kPi = 3.14159265358979323846;
		constexpr double kRadToDeg = 180.0 / kPi;

		Vec3 subtract(const Vec3& a, const Vec3& b) { return Vec3{ a.x - b.x, a.y - b.y, a.z - b.z }; }
		double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		Vec3 cross(const Vec3& a, const Vec3& b) { return Vec3{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
		double length(const Vec3& a) { return std::sqrt(dot(a, a)); }

		// Angle between two directions of any length. atan2 stays accurate for tiny angles, where acos doesn't.
		double angleBetween(const Vec3& a, const Vec3& b) { return std::atan2(length(cross(a, b)), dot(a, b)) * kRadToDeg; }

		double yawOf(const Vec3& v) { return std::atan2(v.y, v.x) * kRadToDeg; }
		double pitchOf(const Vec3& v) { return std::atan2(v.z, std::sqrt(v.x * v.x + v.y * v.y)) * kRadToDeg; }

		double wrapDegrees(double angle)
		{
			while (angle > 180.0) angle -= 360.0;
			while (angle < -180.0) angle += 360.0;
			return angle;
		}
	}

	const char* kindName(Kind kind)
	{
		switch (kind)
		{
		case Kind::Snap: return "Snap-to-target";
		case Kind::Reaction: return "Inhuman reaction";
		case Kind::Robotic: return "Robotic tracking";
		case Kind::Wall: return "Tracking through walls";
		case Kind::Accuracy: return "Impossible accuracy";
		default: return "Unknown";
		}
	}

	Analyzer::Analyzer(const Config& initialConfig)
		: config(initialConfig)
	{
	}

	void Analyzer::reset()
	{
		score = 0.0;
		std::fill(std::begin(counts), std::end(counts), 0);
		pending.clear();
		states.clear();
		hasPrevious = false;
		recentTurns.clear();
		snapArmed = false;
		lastShotTime = -1e9;
		shots.clear();
		lastAccuracyFlag = -1e9;
	}

	std::vector<Detection> Analyzer::takeDetections()
	{
		std::vector<Detection> taken;
		taken.swap(pending);
		return taken;
	}

	void Analyzer::flag(Kind kind, double now, const std::string& detail)
	{
		const int index = static_cast<int>(kind);
		counts[index] += 1;
		score = std::fmin(config.maxSuspicion, score + config.weights[index]);
		pending.push_back(Detection{ kind, now, detail });
	}

	void Analyzer::checkReaction(TargetState& state, double now, bool on)
	{
		// Judge only the first time the aim reaches a target after it comes into view.
		if (on && !state.on && state.visibleSince >= 0.0)
		{
			const double reaction = now - state.visibleSince;
			if (state.errorWhenSeen >= config.reactionMinStartDeg && reaction < config.reactionMaxSeconds)
			{
				char text[160];
				std::snprintf(text, sizeof(text), "on target %.0f ms after it appeared %.0f deg away", reaction * 1000.0, state.errorWhenSeen);
				flag(Kind::Reaction, now, text);
			}
			state.visibleSince = -1.0;
		}
		state.on = on;
	}

	void Analyzer::addSample(double now, const Vec3& eye, const Vec3& aimDirection, const std::vector<TargetSample>& targets)
	{
		const double dt = hasPrevious ? std::fmax(0.0, now - previousTime) : 0.0;
		score = std::fmax(0.0, score - config.decayPerSecond * dt);

		// A snap is one big turn out of stillness.
		const double turn = hasPrevious ? angleBetween(previousDirection, aimDirection) : 0.0;
		double leadIn = 0.0;
		for (const TurnPoint& recent : recentTurns)
		{
			if (now - recent.time < config.snapLeadInSeconds)
			{
				leadIn = std::fmax(leadIn, recent.degrees);
			}
		}
		const bool snapCandidate = hasPrevious && dt <= config.snapMaxUpdateGap && turn >= config.snapMinDeg && leadIn <= turn * config.snapLeadInFraction;
		recentTurns.push_back(TurnPoint{ now, turn });
		while (now - recentTurns.front().time > 0.5)
		{
			recentTurns.pop_front();
		}

		const double aimYaw = yawOf(aimDirection);
		const double aimPitch = pitchOf(aimDirection);
		bool landedDeadCenter = false;
		char text[160];

		for (const TargetSample& target : targets)
		{
			TargetState& state = states[target.id];
			state.lastSample = now;
			state.lastAimPoint = target.aimPoint;
			state.lastRadius = target.radius;

			const Vec3 toTarget = subtract(target.aimPoint, eye);
			const double distance = length(toTarget);
			if (distance <= 0.0)
			{
				continue;
			}
			const double radiusDeg = std::atan2(target.radius, distance) * kRadToDeg;
			const double error = angleBetween(aimDirection, toTarget);
			const bool on = error < radiusDeg * config.onTargetRadii;

			if (snapCandidate && error < radiusDeg * config.snapCenterRadii)
			{
				landedDeadCenter = true;
			}

			if (target.visible && !state.wasVisible)
			{
				state.visibleSince = now;
				state.errorWhenSeen = error;
			}
			if (!target.visible)
			{
				state.visibleSince = -1.0;
			}
			checkReaction(state, now, on);

			// Robotic tracking: how much the aim error wobbles while following a moving target.
			if (on && target.visible && target.moving)
			{
				const double targetPitch = pitchOf(toTarget);
				state.track.push_back(TrackPoint{ now, wrapDegrees(aimYaw - yawOf(toTarget)) * std::cos(targetPitch / kRadToDeg), aimPitch - targetPitch, radiusDeg });
				while (now - state.track.front().time > config.roboticWindow + 0.1)
				{
					state.track.pop_front();
				}
				const double span = now - state.track.front().time;
				if (span >= config.roboticWindow && now - state.lastRobotic > 2.0)
				{
					const double n = static_cast<double>(state.track.size());
					double meanYaw = 0.0, meanPitch = 0.0, meanRadius = 0.0;
					for (const TrackPoint& p : state.track)
					{
						meanYaw += p.yawError / n;
						meanPitch += p.pitchError / n;
						meanRadius += p.radiusDeg / n;
					}
					double variance = 0.0;
					for (const TrackPoint& p : state.track)
					{
						variance += ((p.yawError - meanYaw) * (p.yawError - meanYaw) + (p.pitchError - meanPitch) * (p.pitchError - meanPitch)) / n;
					}
					const double wobble = std::sqrt(variance);
					if (wobble < config.roboticWobbleRadii * meanRadius)
					{
						std::snprintf(text, sizeof(text), "aim wobbled only %.3f deg over %.1f s of tracking", wobble, span);
						flag(Kind::Robotic, now, text);
						state.lastRobotic = now;
					}
				}
			}
			else
			{
				state.track.clear();
			}

			// Through walls: following a hidden target as it moves.
			const double sweep = state.hasLastDirection ? angleBetween(state.lastDirection, toTarget) : 0.0;
			const double tolerance = std::fmax(config.wallMinToleranceDeg, radiusDeg * config.wallToleranceRadii);
			if (!target.visible && target.moving && error < tolerance)
			{
				state.hiddenFollow += dt;
				state.hiddenSweep += sweep;
				if (state.hiddenFollow >= config.wallMinSeconds && state.hiddenSweep >= config.wallMinSweepDeg && now - state.lastWall > 1.5)
				{
					std::snprintf(text, sizeof(text), "followed a hidden target for %.1f s across %.0f deg", state.hiddenFollow, state.hiddenSweep);
					flag(Kind::Wall, now, text);
					state.lastWall = now;
					state.hiddenFollow = 0.0;
					state.hiddenSweep = 0.0;
				}
			}
			else
			{
				state.hiddenFollow = 0.0;
				state.hiddenSweep = 0.0;
			}
			state.lastDirection = toTarget;
			state.hasLastDirection = true;
			state.wasVisible = target.visible;
		}

		// Targets that just stopped being reported (a one-shot kill removes them before the aim update
		// that killed them arrives) are still checked for snaps and reaction time where they last were.
		for (auto it = states.begin(); it != states.end();)
		{
			TargetState& lost = it->second;
			if (lost.lastSample >= now)
			{
				++it;
				continue;
			}
			if (now - lost.lastSample > config.lostTargetSeconds)
			{
				it = states.erase(it);
				continue;
			}
			const Vec3 toLost = subtract(lost.lastAimPoint, eye);
			const double lostDistance = length(toLost);
			if (lostDistance > 0.0)
			{
				const double lostRadiusDeg = std::atan2(lost.lastRadius, lostDistance) * kRadToDeg;
				const double lostError = angleBetween(aimDirection, toLost);
				if (snapCandidate && lostError < lostRadiusDeg * config.snapCenterRadii)
				{
					landedDeadCenter = true;
				}
				checkReaction(lost, now, lostError < lostRadiusDeg * config.onTargetRadii);
			}
			++it;
		}

		// Snap-to-target needs a shot to go with the turn. The shot can reach the server just before or just after it.
		if (snapArmed && now - snapTime > config.snapFireWindow)
		{
			snapArmed = false;
		}
		if (landedDeadCenter)
		{
			if (now - lastShotTime <= config.snapShotBeforeWindow)
			{
				std::snprintf(text, sizeof(text), "turned %.0f deg in one update onto a target's head as it fired", turn);
				flag(Kind::Snap, now, text);
			}
			else
			{
				snapArmed = true;
				snapTime = now;
				snapTurn = turn;
			}
		}

		previousDirection = aimDirection;
		previousTime = now;
		hasPrevious = true;
	}

	void Analyzer::addShot(double now, bool hit)
	{
		char text[160];
		if (snapArmed && now - snapTime <= config.snapFireWindow)
		{
			std::snprintf(text, sizeof(text), "turned %.0f deg in one update onto a target's head, fired %.0f ms later", snapTurn, (now - snapTime) * 1000.0);
			flag(Kind::Snap, now, text);
			snapArmed = false;
		}
		lastShotTime = now;

		shots.push_back(hit);
		while (static_cast<int>(shots.size()) > config.accuracyWindow)
		{
			shots.pop_front();
		}
		if (static_cast<int>(shots.size()) >= config.accuracyMinShots && now - lastAccuracyFlag > 5.0)
		{
			int hits = 0;
			for (const bool wasHit : shots)
			{
				hits += wasHit ? 1 : 0;
			}
			const double rate = static_cast<double>(hits) / static_cast<double>(shots.size());
			if (rate >= config.accuracyThreshold)
			{
				std::snprintf(text, sizeof(text), "%d of the last %d shots hit", hits, static_cast<int>(shots.size()));
				flag(Kind::Accuracy, now, text);
				lastAccuracyFlag = now;
			}
		}
	}
}
