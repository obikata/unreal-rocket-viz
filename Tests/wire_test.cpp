// Decodes every golden packet with UrvWire.h and prints it in a canonical text
// form; Tests/test_wire.py compares that with the Python reference decoder.
// Build: g++ -std=c++17 -Wall -Wextra -I Source/UnrealRocketViz/Public Tests/wire_test.cpp -o wire_test
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#include "UrvWire.h"

int main(int argc, char** argv)
{
	int Bad = 0;
	for (int a = 1; a < argc; ++a)
	{
		std::ifstream F(argv[a], std::ios::binary);
		std::vector<uint8_t> B((std::istreambuf_iterator<char>(F)), std::istreambuf_iterator<char>());
		UrvWire::FMessage M;
		std::string Err;
		if (!UrvWire::Decode(B.data(), B.size(), M, &Err))
		{
			std::printf("%s: ERROR %s\n", argv[a], Err.c_str());
			++Bad;
			continue;
		}
		const auto& H = M.Header;
		std::printf("%s: type=%u seq=%u sender=%u\n", argv[a], H.Type, H.Seq, H.SenderId);
		if (H.Type == 1)
		{
			std::printf("  t=%.17g send=%.17g n=%zu\n", M.SimTime, M.SendTime, M.Entities.size());
			for (const auto& E : M.Entities)
			{
				std::printf("  id=%d pos=%.17g,%.17g,%.17g vel=%.17g,%.17g,%.17g q=%.17g,%.17g,%.17g,%.17g "
					"w=%.17g,%.17g,%.17g gimbal=%.17g,%.17g thr=%.17g mask=%u flags=%u\n",
					E.Id, E.Pos[0], E.Pos[1], E.Pos[2], E.Vel[0], E.Vel[1], E.Vel[2], E.Q[0], E.Q[1], E.Q[2], E.Q[3],
					E.Omega[0], E.Omega[1], E.Omega[2], E.Gimbal[0], E.Gimbal[1], E.Throttle, E.EngineMask, E.Flags);
				for (const auto& C : E.Channels) std::printf("    %s=%.17g\n", C.first.c_str(), C.second);
			}
		}
		else if (H.Type == 2)
			std::printf("  t=%.17g id=%u entity=%d name=%s\n", M.SimTime, M.EventId, M.EventEntity, M.EventName.c_str());
		else
			std::printf("  json=%s\n", M.SceneJson.c_str());
	}
	// Malformed input must be rejected, never read out of bounds.
	{
		std::ifstream F(argv[1], std::ios::binary);
		std::vector<uint8_t> B((std::istreambuf_iterator<char>(F)), std::istreambuf_iterator<char>());
		UrvWire::FMessage M;
		for (std::size_t n = 0; n < B.size(); ++n)
			if (UrvWire::Decode(B.data(), n, M)) { std::printf("TRUNCATED %zu ACCEPTED\n", n); ++Bad; }
	}
	UrvWire::FEventDedup D;
	if (!D.Accept(1, 3) || D.Accept(1, 3) || !D.Accept(2, 3)) { std::printf("DEDUP BROKEN\n"); ++Bad; }
	return Bad;
}
