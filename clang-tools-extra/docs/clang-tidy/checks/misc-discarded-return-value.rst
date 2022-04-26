.. title:: clang-tidy - misc-discarded-return-value

misc-discarded-return-value
===========================

Flags function calls which return value is discarded if most of the other calls
to the function consume the return value.

.. code-block:: c++
    #include <algorithm>

    void good_remove() {
      Container<T> C;

      // Return value of remove_if used.
      auto EraseIt = std::remove_if(C.begin(), C.end(), Predicate);
      std::remove(EraseIt, C.end());
    }

    void bad_remove() {
      Container<T> C;
      // Unused, discarded return value.
      std::remove_if(C.begin(), C.end(), Predicate);
    }

The check is based off of a statistical approach.
If at least `Threshold` percent of calls are consumed and not discarded, the
remaining calls are flagged.
The suggestion is to either consume the value (as in the case of ``remove_if``
above), or use an explicit silencing ``(void)`` cast, like in the case of
``[[nodiscard]]``.

Options
-------

.. option:: ConsumeThreshold

    The percentage of calls that must be consumed for the check to trigger on
    the discarded calls.
    Must be a whole number between `0` and `100`, indicating 0% and 100%,
    respectively.
    Defaults to `80` (80%).
