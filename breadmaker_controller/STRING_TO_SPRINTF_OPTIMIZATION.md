# String to sprintf Memory Optimization

## Memory Efficiency Improvements

Replaced inefficient `String()` concatenations with `sprintf()` using stack-allocated buffers for significant memory savings.

### Optimized Endpoints:

#### 1. **PID Parameters Endpoint** (`/api/pid`)
**Before:** 6 String concatenations + temporary String objects
```cpp
server.sendContent("\"kp\":" + String(pid.Kp, 6) + ",");
server.sendContent("\"ki\":" + String(pid.Ki, 6) + ",");
// ... 4 more similar lines
```

**After:** Single 128-byte stack buffer, reused for all values
```cpp
char buffer[128];  // Stack allocated, much more efficient
sprintf(buffer, "\"kp\":%.6f,", pid.Kp);
server.sendContent(buffer);
sprintf(buffer, "\"ki\":%.6f,", pid.Ki);
server.sendContent(buffer);
// ... etc
```

#### 2. **EWMA Parameters Endpoint** (`/api/pid_params`)
**Before:** 12+ String concatenations for temperature data
**After:** Single 128-byte stack buffer with sprintf formatting

#### 3. **Toggle Endpoints** (heater/motor/light/buzzer)
**Before:** String concatenation for boolean values
```cpp
server.send(200, "application/json", "{\"heater\":" + String(heaterState ? "true" : "false") + "}");
```

**After:** Static string literals (zero dynamic allocation)
```cpp
server.send(200, "application/json", heaterState ? "{\"heater\":true}" : "{\"heater\":false}");
```

#### 4. **Schedule Endpoint**
**Before:** Complex String concatenation for schedule responses
**After:** sprintf with 128-byte stack buffer

## Memory Savings Analysis:

### Per String() Object:
- **Overhead:** ~24 bytes per String object
- **Buffer:** Dynamic allocation based on content
- **Fragmentation:** Each creates/destroys heap memory

### sprintf() Approach:
- **Overhead:** 0 bytes (stack allocated)
- **Buffer:** Fixed 128 bytes on stack (reused)
- **Fragmentation:** Zero heap impact

### Estimated Savings:
- **PID endpoint:** ~200+ bytes saved per request
- **EWMA endpoint:** ~400+ bytes saved per request  
- **Toggle endpoints:** ~50+ bytes saved per request
- **Schedule endpoint:** ~100+ bytes saved per request

### Benefits:
1. **Reduced Heap Fragmentation** - No dynamic String allocations
2. **Faster Execution** - Stack allocation vs heap allocation
3. **Lower Peak Memory** - No temporary String objects
4. **More Predictable Memory Usage** - Fixed stack buffers
5. **Reduced GC Pressure** - No String cleanup needed

## Critical Endpoints Prioritized:
✅ PID parameters (frequent polling)
✅ EWMA temperature data (real-time monitoring)  
✅ Toggle controls (user interactions)
✅ Schedule responses (program control)
⏸️ Debug endpoints (development only, less critical)

## Result:
The most frequently called endpoints now use minimal dynamic memory allocation, significantly reducing heap fragmentation and improving system stability during long bread-making cycles.
