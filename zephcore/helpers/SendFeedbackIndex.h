/*
 * SPDX-License-Identifier: MIT
 * SendFeedbackIndex — timestamp -> packet hash of recent channel sends.
 * Lets a later query ask ContentionTracker how many neighbours repeated
 * each flood (extractDupeCount). Plain FIFO ring, oldest evicted.
 */

#pragma once

#include <stdint.h>

class SendFeedbackIndex {
public:
	static constexpr int SLOTS = 4;

	void add(uint32_t ts, uint32_t hash)
	{
		if (_count == SLOTS) {
			for (int i = 1; i < SLOTS; i++) {
				_entries[i - 1] = _entries[i];
			}
			_count--;
		}
		_entries[_count].ts = ts;
		_entries[_count].hash = hash;
		_count++;
	}

	/* Pops the oldest entry matching ts. False when none left. */
	bool pop(uint32_t ts, uint32_t &hash)
	{
		for (int i = 0; i < _count; i++) {
			if (_entries[i].ts != ts) {
				continue;
			}
			hash = _entries[i].hash;
			for (int j = i + 1; j < _count; j++) {
				_entries[j - 1] = _entries[j];
			}
			_count--;
			return true;
		}
		return false;
	}

private:
	struct Entry {
		uint32_t ts;
		uint32_t hash;
	};
	Entry _entries[SLOTS];
	int _count = 0;
};
