Monitor inflation and the monitor pool
--------------------------------------

Every Java object conceptually has an associated monitor that is used to implement `synchronized`
blocks as well as `Object.wait()` and related functionality. Initially, the state of this monitor
is represented by the 32-bit `LockWord` in every object. This same word is also used to represent
the system hash code if one has been computed for the object. See `lock_word.h` for details.

The `LockWord` suffices for representing the state of unlocked objects with no waiters, whether or
not they have a system hash code. It can also represent a locked (via a `synchronized` block)
object that has neither a system hash code, nor an `Object.wait()` waiter. In all other cases, the
lock must be "inflated". In that case, the `LockWord` contents reflect the "fat lock" state, and
it contains primarily a `MonitorId`. We also inflate significantly contended locks, since the lock
word itself does not have space for a wait queue or the like.

(With the Concurrent Copying garbage collector, the `LockWord` supports another "forwarding
address" state. In that case the above information is instead contained in the `LockWord` for the
to-space objects referenced by the forwarding address.)

MonitorIds
----------
A `MonitorId` is logically a pointer to a `Monitor` structure. The `Monitor` data structure is
allocated when needed, and holds all the data needed to support any hashcode or `Object`
synchronization operation provided by Java. On 32-bit implementations, it is essentially a
pointer, except for some shifting and masking to avoid collisions with a few bits in the lock word
required for other purposes. In a 64-bit environment, the situation is more complicated, since we
don't have enough bits in the lock word to store a 64-bit pointer.

In the 64-bit case, `Monitor`s are stored in "chunks" of 4K bytes. The bottom bits of a
`MonitorId` are the index of the `Monitor` within its chunk. The remaining bits are used to find
the correct chunk.

Continuing with the 64-bit case, chunk addresses are stored in the `monitor_chunks_` data
structure, which is logically a two-dimensional, vaguely triangular, array of pointers to chunks
Each row is allocated separately, and twice as long as the previous one. (Thus the array is not
really triangular.) The high bits of a `MonitorId` are interpreted as row- and column-indices into
this array. Both the second level "rows" of this array, and the chunks themselves are allocated on
demand.  By making the rows grow exponentially in size, and keeping a free list of recycled
`Monitor` slots, we can accommodate a large number of `Monitor`s, without ever allocating more than
twice the index size that was actually required.  (This doesn't count the top-level index needed
to find the rows, but exponential growth of the rows makes that tiny.) And there is no need to
ever move `Monitor` objects, which would significantly complicate the logic.

The above data structure is distinct from the `MonitorList` data structure, which is used simply
to enumerate all monitors. (It might be possible to save a bit of space in the 64-bit case, and
have the `monitor_chunks_` data structure handle this as well.)

Monitor inflation
-----------------
Monitors are inflated, and `Monitor` structs are allocated on demand, when an operation is
performed that cannot be accommodated with just the lock word. This normally happens when we need
to store a hash code in a locked object, when there is lock contention, or when `Object.wait` is
executed. (Notification on an object with no waiters is trivial and does not require inflation.)
In the 64-bit case, the `monitor_chunks_` data structure may also need to be extended at this time
to allow mapping of an additional `MonitorId`.

If we have to inflate a lock that is currently held by another Thread B, we must suspend B while
updating the data structure representing the lock B holds. When the lock is later released by B,
it will notice the change and operate on the fat lock representation instead.

Monitor deflation
-----------------
Monitors are deflated, and the `Monitor` structs associated with deflated monitors are reclaimed
as part of `Heap::Trim()` by invoking `SweepMonitorList()` with an `IsMarkedVisitor` that deflates
unheld monitors with no waiters. This is done with all other threads suspended.

Monitors are also reclaimed, again via `SweepMonitorList()`, in `SweepSystemWeaks()`, if the
corresponding object was not marked.

(There is one other use of monitor deflation from `ImageWriter`. That does not maintain
`MonitorList`. It relies on the fact that the dex2oat process is single-threaded, and the heap is
about to be discarded.)

