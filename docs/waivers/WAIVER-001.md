---
title: "WAIVER-001: Deopt path overhead in Virtual Storm"
status: "Draft"
owner: "JADE Dev Team"
last_updated: "2026-08-19"
related_rules: ["Rule 41"]
pass_type: "Waiver"
tier: "RUBY"
---

# WAIVER-001: Deopt path overhead in Virtual Storm

**Status:** Draft (example template — not a real waiver)  
**Owner:** JADE Dev Team  
**Last Updated:** 2026-08-19  
**Related Rules:** Rule 41 (Performance regression waiver)

---

## Example Waiver Document

This is an example of a Rule 41 waiver. Real waivers must follow this template.

## Root Cause

The deopt handler does a linear scan of the FrameState table. With 64 simultaneous deopting threads, this becomes O(N) per deopt.

## Justification

The fix (hash table) is planned for sprint 4. Until then, the regression is acceptable because the affected benchmark (`Virtual Storm`) is not in the customer workload.

## Performance Plan

Sprint 4 will replace the linear scan with a hash table. Expected speedup: 1.5×.

## Tracking Issue

#1234 — Replace linear FrameState scan with hash table.

## Removal Deadline

2026-10-20 (60 days from approval).
