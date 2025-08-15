# Program Selection Status - Clarification

## Current Program Status ✅

**Selected Program**: Bread - Sourdough In-Machine  
**Program ID**: 1  
**Current Stage**: Initial Mix (Stage 0)  
**Status**: Ready to start (not running)

## Program ID Mapping (Confirmed Correct)

| ID | Program Name |
|----|--------------|
| 0  | Bread - Sourdough Basic |
| **1**  | **Bread - Sourdough In-Machine** ← CURRENTLY SELECTED |
| 2  | Bread - Sourdough Pizza Dough |
| 3  | Bread - White Basic |
| 4  | Bread - Whole Wheat |
| 5  | Bread - Enriched Sweet |
| 6  | Bread - Rye |

## Program Stages for "Bread - Sourdough In-Machine" (ID 1)

1. **Initial Mix** (20 min) - Pre-mix flour and water
2. **Autolyse** (30 min) - Add flour and water
3. **Mix** (106 min) - Add starter and salt with mixing patterns
4. **Bulk Ferment** (300 min) - Primary fermentation
5. **Knockdown** (5 min) - Degas dough
6. **Proof** (720 min) - Final proofing before baking
7. **Bake 1** (30 min) - First bake at 175°C
8. **Bake 2** (35 min) - Second bake at 175°C
9. **Cool** (10 min) - Cooling period

## Issue Resolution

The confusion may have occurred because:
1. **Before the fix**: The web interface wasn't showing program names correctly due to the broken `/api/programs` endpoint
2. **Previous state**: The system may have had a different program selected before we verified the correct selection
3. **Fixed state**: Now showing correct program ID 1 = "Bread - Sourdough In-Machine"

## Verification Commands Used

```bash
# Verify program list and IDs
curl http://192.168.250.125/api/programs

# Select specific program by ID
curl "http://192.168.250.125/select?idx=1"

# Check current status
curl http://192.168.250.125/api/status
```

## Current Temperature & Safety

- **Current Temperature**: 22.3°C
- **All temperatures safely below 180°C thermal fuse limit** ✅
- **Maximum baking temperature**: 175°C (safe 5°C margin)

## Next Steps

1. ✅ **Program correctly selected**: "Bread - Sourdough In-Machine" (ID 1)
2. ✅ **System ready to start**: Use `/start` endpoint or web interface Start button
3. ✅ **Temperature monitoring active**: Real-time updates available
4. ✅ **All safety limits enforced**: Thermal fuse protection in place

**Status**: Program selection verified correct - ready for operation

**Date**: August 16, 2025
