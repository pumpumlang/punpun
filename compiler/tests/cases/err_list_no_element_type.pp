// A list with no element type has no meaning, and there is nothing at this call
// site to infer one from.
launch { let items = list(); say(1); }
