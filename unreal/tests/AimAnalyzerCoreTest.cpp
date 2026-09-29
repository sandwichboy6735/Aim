// Runs the aim analyzer against simulated players, outside Unreal.
// From the repo root:
//   g++ -std=c++17 -O2 -I unreal/LockOn unreal/tests/AimAnalyzerCoreTest.cpp unreal/LockOn/AimAnalyzerCore.cpp -o aimtest && ./aimtest
// Pass a number to try a different random seed: ./aimtest 42
//
// Simulated humans must not trip the snap, reaction, robotic or wall checks, and must stay under
// "Suspicious" (25). Good players on close targets can trip the accuracy check, which is why it
// can't push anyone past "Clean" on its own. Each bot mode must trip the check built for it.
#include "AimAnalyzerCore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using aimcore::Analyzer;
using aimcore::Kind;
using aimcore::TargetSample;
using aimcore::Vec3;

namespace
{
	constexpr double kPi = 3.14159265358979323846;
	constexpr double kDegToRad = kPi / 180.0;
	constexpr double kDt = 1.0 / 60.0;
	constexpr double kHeadRadius = 12.0;
	constexpr int kKinds = static_cast<int>(Kind::Count);
	const Vec3 kEye{ 0.0, 0.0, 160.0 };

	enum class Player { Human, FastHuman, HoldAngle, Snap, Smooth, SmoothNoFire, SmoothThroughWalls };

	const char* playerName(Player player)
	{
		switch (player)
		{
		case Player::Human: return "Human (steady)";
		case Player::FastHuman: return "Human (fast flicks)";
		case Player::HoldAngle: return "Human (holds an angle)";
		case Player::Snap: return "Bot: Snap";
		case Player::Smooth: return "Bot: Smooth";
		case Player::SmoothNoFire: return "Bot: Smooth, no fire";
		case Player::SmoothThroughWalls: return "Bot: Smooth + walls";
		}
		return "?";
	}

	Vec3 direction(double yaw, double pitch)
	{
		const double y = yaw * kDegToRad, p = pitch * kDegToRad;
		return Vec3{ std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p) };
	}

	double wrap(double angle)
	{
		while (angle > 180.0) angle -= 360.0;
		while (angle < -180.0) angle += 360.0;
		return angle;
	}

	double errorDeg(double yaw, double pitch, const Vec3& point)
	{
		const Vec3 aim = direction(yaw, pitch);
		const double dx = point.x - kEye.x, dy = point.y - kEye.y, dz = point.z - kEye.z;
		const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
		const double d = std::clamp((aim.x * dx + aim.y * dy + aim.z * dz) / len, -1.0, 1.0);
		return std::acos(d) / kDegToRad;
	}

	// Minimum-jerk position profile, the shape of a fast human hand movement.
	double minJerk(double s)
	{
		s = std::clamp(s, 0.0, 1.0);
		return s * s * s * (10.0 - 15.0 * s + 6.0 * s * s);
	}

	struct Target
	{
		int id = 0;
		bool alive = false;
		Vec3 pos;
		Vec3 vel;
		double born = 0.0;
		double hiddenUntil = 0.0;
		double respawnAt = 0.0;
		double nextTurn = 0.0;
	};

	struct Result
	{
		int counts[kKinds] = {};
		double peak = 0.0;
		int shots = 0;
		int hits = 0;
		double seconds = 0.0;
		int sampleHz = 0;
	};

	Result run(Player player, double seconds, int sampleEvery, unsigned seed)
	{
		std::mt19937 rng(seed);
		auto uniform = [&rng](double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(rng); };
		auto gauss = [&rng](double mean, double sd) { return std::normal_distribution<double>(mean, sd)(rng); };

		Analyzer analyzer;
		Target target;
		int nextId = 1;
		double yaw = 0.0, pitch = 0.0;           // where the player is aiming
		double yawVel = 0.0, pitchVel = 0.0;     // hand velocity, deg/s
		double noiseYaw = 0.0, noisePitch = 0.0; // hand tremor and tracking error
		int engaged = 0;
		double reactAt = 0.0, fireDelay = 0.0, onSince = -1.0, lastFire = -10.0;
		bool flickDone = false;
		double flickStart = 0.0, flickTime = 0.0, fromYaw = 0.0, fromPitch = 0.0, missYaw = 0.0, missPitch = 0.0;
		const bool walls = player == Player::SmoothThroughWalls;
		const bool human = player == Player::Human || player == Player::FastHuman || player == Player::HoldAngle;
		Result result;
		result.seconds = seconds;
		result.sampleHz = static_cast<int>(std::lround(60.0 / sampleEvery));

		for (int step = 0; step * kDt < seconds; ++step)
		{
			const double t = step * kDt;

			// One strafing enemy at a time, appearing 15-40 degrees off the crosshair, 10-30 m away.
			if (!target.alive && t >= target.respawnAt && player == Player::HoldAngle)
			{
				// Slow enemies walk behind a wall, straight through the spot this player is aiming at.
				const double distance = uniform(150, 600);
				const double side = uniform(0, 1) < 0.5 ? -1.0 : 1.0;
				target.id = nextId++;
				target.alive = true;
				target.pos = Vec3{ distance, side * uniform(60, 120), 160.0 + uniform(-10, 10) };
				target.vel = Vec3{ 0.0, -side * uniform(55, 120), 0.0 };
				target.born = t;
				target.hiddenUntil = 1e9;
				target.nextTurn = 1e9;
			}
			else if (!target.alive && t >= target.respawnAt)
			{
				const double bearing = (yaw + (uniform(0, 1) < 0.5 ? -1.0 : 1.0) * uniform(15, 40)) * kDegToRad;
				const double distance = uniform(1000, 3000);
				const double speed = uniform(150, 450) * (uniform(0, 1) < 0.5 ? -1.0 : 1.0);
				target.id = nextId++;
				target.alive = true;
				target.pos = Vec3{ std::cos(bearing) * distance, std::sin(bearing) * distance, 160.0 + uniform(-30, 30) };
				target.vel = Vec3{ -std::sin(bearing) * speed, std::cos(bearing) * speed, 0.0 };
				target.born = t;
				target.hiddenUntil = walls ? t + 1.2 : t;
				target.nextTurn = t + uniform(0.6, 1.4);
			}
			if (target.alive)
			{
				target.pos.x += target.vel.x * kDt;
				target.pos.y += target.vel.y * kDt;
				if (t >= target.nextTurn)
				{
					target.vel = Vec3{ -target.vel.x, -target.vel.y, 0.0 };
					target.nextTurn = t + uniform(0.6, 1.4);
				}
				if (t - target.born > 3.0)
				{
					target.alive = false;
					target.respawnAt = t + 0.4;
				}
			}
			const bool visible = target.alive && t >= target.hiddenUntil;

			// The server samples the aim the client last sent.
			if (step % sampleEvery == 0)
			{
				std::vector<TargetSample> samples;
				if (target.alive)
				{
					samples.push_back(TargetSample{ target.id, target.pos, kHeadRadius, visible, true });
				}
				analyzer.addSample(t, kEye, direction(yaw, pitch), samples);
				result.peak = std::max(result.peak, analyzer.suspicion());
			}

			// Hand tremor / tracking error for the simulated humans (an Ornstein-Uhlenbeck process).
			const double tremorSd = player == Player::FastHuman ? 0.3 : 0.35, tau = 0.15;
			noiseYaw += -noiseYaw / tau * kDt + tremorSd * std::sqrt(2.0 / tau * kDt) * gauss(0, 1);
			noisePitch += -noisePitch / tau * kDt + tremorSd * std::sqrt(2.0 / tau * kDt) * gauss(0, 1);

			if (player == Player::HoldAngle)
			{
				yaw = noiseYaw;
				pitch = noisePitch;
				continue;
			}
			if (!target.alive)
			{
				yawVel *= 0.8;
				pitchVel *= 0.8;
				yaw += yawVel * kDt;
				pitch += pitchVel * kDt;
				continue;
			}

			const double dx = target.pos.x - kEye.x, dy = target.pos.y - kEye.y, dz = target.pos.z - kEye.z;
			const double targetYaw = std::atan2(dy, dx) / kDegToRad;
			const double targetPitch = std::atan2(dz, std::hypot(dx, dy)) / kDegToRad;
			const double radiusDeg = std::atan2(kHeadRadius, std::sqrt(dx * dx + dy * dy + dz * dz)) / kDegToRad;
			const bool canSee = visible || walls;

			if (target.id != engaged && canSee)
			{
				engaged = target.id;
				onSince = -1.0;
				flickDone = false;
				switch (player)
				{
				case Player::Human: reactAt = t + std::clamp(gauss(0.26, 0.05), 0.18, 0.45); break;
				case Player::FastHuman: reactAt = t + std::clamp(gauss(0.2, 0.03), 0.15, 0.3); break;
				case Player::Snap: reactAt = t; fireDelay = 0.0; break;
				default: reactAt = t; fireDelay = 0.04; break;
				}
			}

			bool wantsFire = false;
			if (engaged == target.id && canSee && t >= reactAt)
			{
				switch (player)
				{
				case Player::Human:
				case Player::FastHuman:
				{
					if (player == Player::FastHuman && !flickDone)
					{
						if (flickTime == 0.0)
						{
							flickStart = t;
							fromYaw = yaw;
							fromPitch = pitch;
							const double amplitude = std::hypot(wrap(targetYaw - yaw), targetPitch - pitch);
							flickTime = 0.06 + 0.001 * amplitude;
							missYaw = gauss(0, 0.8 * radiusDeg);
							missPitch = gauss(0, 0.8 * radiusDeg);
						}
						const double s = minJerk((t + kDt - flickStart) / flickTime);
						yaw = fromYaw + s * wrap(targetYaw + missYaw - fromYaw);
						pitch = fromPitch + s * (targetPitch + missPitch - fromPitch);
						if (s >= 1.0)
						{
							flickDone = true;
							flickTime = 0.0;
							yawVel = pitchVel = 0.0;
						}
					}
					else
					{
						// Critically damped spring toward the target, plus tremor.
						const double stiffness = player == Player::FastHuman ? 20.0 : 14.0;
						const double yawAcc = stiffness * stiffness * wrap(targetYaw + noiseYaw - yaw) - 2.0 * stiffness * yawVel;
						const double pitchAcc = stiffness * stiffness * (targetPitch + noisePitch - pitch) - 2.0 * stiffness * pitchVel;
						yawVel += yawAcc * kDt;
						pitchVel += pitchAcc * kDt;
						yaw += yawVel * kDt;
						pitch += pitchVel * kDt;
					}
					const double err = errorDeg(yaw, pitch, target.pos);
					const bool landed = player != Player::FastHuman || flickDone;
					const double fireChance = player == Player::FastHuman ? 0.6 : 0.25;
					wantsFire = landed && err < radiusDeg * 1.5 && t - lastFire > 0.2 && uniform(0, 1) < fireChance;
					break;
				}
				case Player::HoldAngle:
					break;
				case Player::Snap:
					yaw = targetYaw;
					pitch = targetPitch;
					break;
				case Player::Smooth:
				case Player::SmoothNoFire:
				case Player::SmoothThroughWalls:
				{
					const double maxStep = (90.0 + 0.5 * 630.0) * kDt; // same as the Unreal bot at Strength 0.5
					yaw += std::clamp(wrap(targetYaw - yaw), -maxStep, maxStep);
					pitch += std::clamp(targetPitch - pitch, -maxStep, maxStep);
					break;
				}
				}

				if (!human && player != Player::SmoothNoFire)
				{
					// Bot auto-fire: on the head for FireDelay seconds, at most every 0.15 s.
					if (errorDeg(yaw, pitch, target.pos) < radiusDeg * 0.6)
					{
						if (onSince < 0.0) onSince = t;
						wantsFire = t - onSince >= fireDelay && t - lastFire >= 0.15;
					}
					else
					{
						onSince = -1.0;
					}
				}
			}

			if (wantsFire)
			{
				lastFire = t;
				const bool hit = visible && errorDeg(yaw, pitch, target.pos) < radiusDeg;
				analyzer.addShot(t, hit);
				result.shots++;
				if (hit)
				{
					result.hits++;
					target.alive = false;
					target.respawnAt = t + 0.5;
				}
			}
		}

		for (int k = 0; k < kKinds; ++k)
		{
			result.counts[k] = analyzer.count(static_cast<Kind>(k));
		}
		return result;
	}

}

int main(int argc, char** argv)
{
	struct Case
	{
		Player player;
		double seconds;
		int sampleEvery; // 1 = the server sees 60 aim updates a second, 3 = 20
		Kind expect;     // the check that must trip, or Kind::Count for a human who must stay clean
	};
	const Case cases[] = {
		{ Player::Human, 600.0, 1, Kind::Count },
		{ Player::FastHuman, 600.0, 1, Kind::Count },
		{ Player::Human, 600.0, 3, Kind::Count },
		{ Player::FastHuman, 600.0, 3, Kind::Count },
		{ Player::HoldAngle, 600.0, 1, Kind::Count },
		{ Player::Snap, 60.0, 1, Kind::Snap },
		{ Player::Snap, 60.0, 3, Kind::Reaction },
		{ Player::Smooth, 60.0, 1, Kind::Reaction },
		{ Player::SmoothNoFire, 60.0, 1, Kind::Robotic },
		{ Player::SmoothThroughWalls, 60.0, 1, Kind::Wall },
	};

	std::printf("%-22s %5s %4s | %4s %8s %7s %4s %8s | %5s %4s %9s | %s\n",
		"Player", "Time", "Hz", "Snap", "Reaction", "Robotic", "Wall", "Accuracy", "Shots", "Hit%", "Peak sus.", "Result");
	int failures = 0;
	unsigned seed = argc > 1 ? static_cast<unsigned>(std::strtoul(argv[1], nullptr, 10)) : 7u;
	for (const Case& c : cases)
	{
		const Result r = run(c.player, c.seconds, c.sampleEvery, seed++);
		bool pass = true;
		if (c.expect == Kind::Count)
		{
			const int strong = r.counts[0] + r.counts[1] + r.counts[2] + r.counts[3];
			pass = strong == 0 && r.peak < 25.0;
		}
		else
		{
			pass = r.counts[static_cast<int>(c.expect)] > 0;
		}
		failures += pass ? 0 : 1;
		std::printf("%-22s %4.0fs %4d | %4d %8d %7d %4d %8d | %5d %3.0f%% %9.0f | %s\n",
			playerName(c.player), r.seconds, r.sampleHz,
			r.counts[0], r.counts[1], r.counts[2], r.counts[3], r.counts[4],
			r.shots, r.shots ? 100.0 * r.hits / r.shots : 0.0, r.peak,
			pass ? "ok" : "FAIL");
	}
	if (failures)
	{
		std::printf("\n%d case(s) failed\n", failures);
		return 1;
	}
	std::printf("\nAll cases passed\n");
	return 0;
}
