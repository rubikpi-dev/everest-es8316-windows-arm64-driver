/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Licensed under the GNU General Public License, version 2 only.
 * Provided without warranty. See LICENSE for the license text.
 */

#ifndef ES8316_RING_FEED_H
#define ES8316_RING_FEED_H

/* The owner thread alone uses this state.  Initialize only after the full
 * zero-filled ring has been accepted as preroll.  Qualcomm's LPA position
 * protocol uses [1, BufferBytes], with BufferBytes representing wrap to zero;
 * zero and duplicate positions do not submit data.  Its DSP writes require
 * 32-byte alignment, and the two initial chunks must each meet that alignment.
 */
typedef struct _ES8316_RING_FEED {
    unsigned long long Consumed;
    unsigned long long Submitted;
    unsigned long BufferBytes;
    unsigned long Quantum;
} ES8316_RING_FEED;

static __inline int
Es8316RingFeedInitialize(ES8316_RING_FEED *State, unsigned long BufferBytes)
{
    if (!State || !BufferBytes || BufferBytes > 0xffffffffUL ||
        (BufferBytes % 64UL) != 0)
        return 0;
    State->Consumed = 0;
    State->Submitted = BufferBytes;
    State->BufferBytes = BufferBytes;
    State->Quantum = 32;
    return 1;
}

/* Returns -1 for invalid/discontinuous progress, 0 for no new completed data,
 * or 1 with a proposed write.  A failure or unchanged poll never changes the
 * state or outputs.  Full-ring catch-up is rejected: it would produce the
 * previous write position and be silently ignored by the Qualcomm miniport.
 */
static __inline int
Es8316RingFeedPlan(const ES8316_RING_FEED *State,
    unsigned long long Consumed, unsigned long *Position,
    unsigned long long *Submitted)
{
    unsigned long long next, advance;
    unsigned long position;
    if (!State || !Position || !Submitted || !State->BufferBytes ||
        (State->BufferBytes % 64UL) != 0 || State->Quantum != 32 ||
        State->Consumed > (~0ULL) - State->BufferBytes ||
        State->Submitted != State->Consumed + State->BufferBytes ||
        Consumed < State->Consumed || Consumed > State->Submitted ||
        (Consumed % State->Quantum) != 0 ||
        Consumed > (~0ULL) - State->BufferBytes)
        return -1;
    if (Consumed == State->Consumed)
        return 0;
    next = Consumed + State->BufferBytes;
    advance = next - State->Submitted;
    if (!advance || advance >= State->BufferBytes)
        return -1;
    position = (unsigned long)(next % State->BufferBytes);
    if (!position)
        position = State->BufferBytes;
    *Position = position;
    *Submitted = next;
    return 1;
}

/* Commit only after the corresponding CURRENT_WRITE_POSITION request has
 * succeeded.  Rechecking rejects stale plans and mismatched totals.
 */
static __inline int
Es8316RingFeedCommit(ES8316_RING_FEED *State,
    unsigned long long Consumed, unsigned long long Submitted)
{
    unsigned long position;
    unsigned long long expected;
    if (Es8316RingFeedPlan(State, Consumed, &position, &expected) != 1 ||
        expected != Submitted)
        return 0;
    State->Consumed = Consumed;
    State->Submitted = Submitted;
    return 1;
}

#endif
