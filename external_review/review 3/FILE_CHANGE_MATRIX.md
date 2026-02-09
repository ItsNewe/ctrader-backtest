# File Change Matrix - cTrader Backtest Critical Fixes

## Overview
This matrix details all files that require modification to address the critical issues identified in the code review. Files are organized by priority and type of change.

## Critical Priority Files (Phase 1)

### C++ Core Files

#### `include/backtest_engine.h`
**Changes Required:**
- Modify `IStrategy::Clone()` method signature to return `std::unique_ptr<IStrategy>`
- Add `RandomGenerator` include and member
- Update `BacktestEngine` constructor to accept random seed parameter
- Add smart pointer usage throughout class

**Specific Lines to Change:**
- Line ~150: `virtual IStrategy* Clone() const = 0;` → `virtual std::unique_ptr<IStrategy> Clone() const = 0;`
- Add include: `#include <memory>`
- Add member: `RandomGenerator random_gen_;`
- Update constructor signature

#### `src/main.cpp`
**Changes Required:**
- Update all strategy `Clone()` implementations to return `std::unique_ptr`
- Replace `rand()` calls with seeded random generator
- Add proper memory management for strategy objects

**Specific Locations:**
- Lines 56, 144, 238: `return new MACrossoverStrategy(params_);` → `return std::make_unique<MACrossoverStrategy>(params_);`
- Line 379: `rand() % (config.max_slippage_points + 1)` → `random_gen_.GenerateSlippage(config.max_slippage_points)`

#### `src/metatrader_connector.cpp`
**Changes Required:**
- Implement RAII for file operations
- Add exception safety to data loading functions
- Replace raw file operations with safe wrappers

**Specific Functions:**
- `LoadBarsFromCSV()` - Add try-catch and RAII file handling
- `LoadTicksFromCSV()` - Same RAII improvements
- `MTHistoryLoader::LoadBarsFromHistory()` - Exception safety

### Python Backend Files

#### `server.py`
**Changes Required:**
- Implement async backtest execution with ThreadPoolExecutor
- Add comprehensive input validation using Pydantic
- Fix SQL injection vulnerabilities in sweep endpoints
- Implement path traversal protection
- Add proper error handling and status tracking

**Specific Endpoints:**
- `/api/backtest/run` - Make async with background execution
- `/api/sweep/<sweep_id>` - Fix SQL injection
- `/api/broker/connect` - Add input validation
- All endpoints - Add rate limiting and security headers

**New Functions Needed:**
- `run_backtest_async()` - Background execution function
- `validate_backtest_config()` - Input validation
- `PathValidator` class - Path security

#### `broker_api.py`
**Changes Required:**
- Encrypt credential storage
- Add secure configuration management
- Implement proper error handling for broker connections
- Add connection timeout and retry logic

**Specific Classes:**
- `BrokerAccount` - Modify to use encrypted credentials
- `CTraderAPI.connect()` - Add timeout and error handling
- `MetaTrader5API.connect()` - Improve error messages and validation

**New Classes:**
- `SecureConfig` - Credential encryption/decryption
- `SecureBrokerAccount` - Encrypted credential storage

### Frontend Files

#### `ui/dashboard.js`
**Changes Required:**
- Implement module pattern instead of global variables
- Add proper error handling for async operations
- Sanitize user inputs before DOM manipulation
- Add client-side validation for form inputs

**Specific Functions:**
- `runBacktest()` - Add error handling and loading states
- `connectBroker()` - Improve error messages and validation
- `fetchPriceHistory()` - Add timeout and retry logic

## High Priority Files (Phase 2)

### Architecture Refactoring

#### `include/backtest_engine.h` (continued)
**Additional Changes:**
- Split `BacktestEngine` into smaller classes
- Implement dependency injection interfaces
- Add proper error handling with custom exceptions

**New Interfaces:**
- `IDataLoader` - Abstract data loading
- `IPositionManager` - Position lifecycle management
- `IMetricsCalculator` - Performance calculation
- `IRiskManager` - Risk assessment

#### `src/backtest_engine.cpp`
**Changes Required:**
- Implement new focused classes
- Add dependency injection
- Replace raw pointers with smart pointers
- Add comprehensive error handling

### Testing Infrastructure

#### New Files: Unit Tests
```
test/
├── cpp/
│   ├── test_backtest_engine.cpp
│   ├── test_random_generator.cpp
│   ├── test_memory_management.cpp
│   └── test_strategy_cloning.cpp
├── python/
│   ├── test_server_validation.py
│   ├── test_broker_security.py
│   ├── test_path_traversal.py
│   └── test_sql_injection.py
└── integration/
    ├── test_full_backtest_pipeline.py
    └── test_broker_integration.py
```

**Test Files Required:**
- Memory leak detection tests
- Random reproducibility tests
- Security vulnerability tests
- Performance regression tests
- Integration pipeline tests

### Configuration Files

#### `config.py` (new)
**Purpose:** Centralized configuration management
**Contents:**
- Environment-specific settings
- Secure credential management
- Feature flags for gradual rollouts
- Configuration validation

#### `constants.h` (new)
**Purpose:** Centralized constants and magic numbers
**Contents:**
- Default backtest parameters
- Broker-specific limits
- System constraints and limits
- Error codes and messages

## Medium Priority Files (Phase 3)

### Documentation Files

#### `api_docs.yaml` (new)
**OpenAPI/Swagger specification**
- Complete API documentation
- Request/response schemas
- Authentication requirements
- Rate limiting information

#### `README.md` (update)
**User documentation**
- Installation instructions
- Configuration guide
- API usage examples
- Troubleshooting guide

#### `ARCHITECTURE.md` (new)
**System architecture documentation**
- Component diagrams
- Data flow diagrams
- Design decision records
- Performance characteristics

### Development Infrastructure

#### `CMakeLists.txt` (update)
**Build system improvements**
- Add Google Test integration
- Enable code coverage reporting
- Add clang-tidy integration
- Performance profiling options

#### `.clang-format` (new)
**Code formatting standards**
- Consistent indentation
- Naming conventions
- Include ordering
- Line length limits

#### `Dockerfile` (new)
**Containerization**
- Multi-stage build for C++ components
- Python environment setup
- Security hardening
- Development and production variants

## Implementation Order

### Week 1: Critical Fixes
1. `include/backtest_engine.h` - IStrategy interface changes
2. `src/main.cpp` - Strategy cloning fixes
3. `server.py` - Security patches (SQL injection, path traversal)
4. `broker_api.py` - Credential encryption

### Week 2: Stability Fixes
5. `server.py` - Async execution and job management
6. `include/random_generator.h` - New random number class
7. `src/metatrader_connector.cpp` - RAII improvements
8. Unit tests for critical fixes

### Month 1: Architecture
9. Split `BacktestEngine` into focused classes
10. Implement dependency injection
11. Smart pointer migration
12. Comprehensive error handling

### Months 2-3: Quality
13. Unit testing framework
14. Logging standardization
15. Configuration management
16. API documentation

### Months 4-6: Advanced Features
17. Monitoring and observability
18. Security hardening
19. Scalability improvements
20. Comprehensive documentation

## Risk Assessment by File

### High Risk Files
- `include/backtest_engine.h` - Core interface changes, backward compatibility
- `server.py` - Security vulnerabilities, API changes
- `src/main.cpp` - Memory management, performance impact

### Medium Risk Files
- `broker_api.py` - External service dependencies, credential handling
- `src/metatrader_connector.cpp` - File I/O, exception safety
- `ui/dashboard.js` - User experience, browser compatibility

### Low Risk Files
- New test files - Isolated, no production impact
- Documentation files - No functional changes
- Configuration files - Controlled rollout possible

## Testing Strategy by File Type

### C++ Files
- **Unit Tests:** Google Test framework
- **Memory Tests:** Valgrind, AddressSanitizer
- **Performance Tests:** Google Benchmark
- **Integration Tests:** Full pipeline testing

### Python Files
- **Unit Tests:** pytest framework
- **Security Tests:** SQL injection, path traversal test suites
- **API Tests:** Flask test client, contract testing
- **Integration Tests:** End-to-end workflow testing

### JavaScript Files
- **Unit Tests:** Jest framework
- **Integration Tests:** Selenium/Cypress for UI testing
- **Performance Tests:** Lighthouse CI
- **Security Tests:** DOM XSS prevention

## Deployment Strategy by File Type

### Immediate Deployment (Critical Fixes)
- Memory leak fixes
- Security patches
- Random generator fixes
- Input validation

### Gradual Rollout (Architecture Changes)
- Dependency injection (feature flags)
- Smart pointer migration (phased rollout)
- Error handling improvements (backward compatible)

### Blue-Green Deployment (Major Changes)
- Database schema changes
- API breaking changes
- Authentication system changes

## Monitoring and Rollback

### Files Requiring Monitoring
- `server.py` - API response times, error rates
- `include/backtest_engine.h` - Memory usage, performance metrics
- `broker_api.py` - Connection success rates, timeout rates

### Rollback Procedures
- **Code Rollback:** Git revert for immediate rollback
- **Feature Flags:** Disable new features without code changes
- **Database Rollback:** Migration rollback scripts
- **Configuration Rollback:** Environment variable overrides

## Success Criteria by File

### Fully Implemented
- [ ] All critical security issues resolved
- [ ] Memory leaks eliminated
- [ ] Random generation reproducible
- [ ] Input validation comprehensive
- [ ] Error handling robust

### Partially Implemented
- [ ] Unit test coverage > 80%
- [ ] API documentation complete
- [ ] Performance benchmarks established
- [ ] Monitoring alerts configured

### Future Enhancements
- [ ] Advanced monitoring implemented
- [ ] Scalability improvements deployed
- [ ] Security hardening complete
- [ ] User documentation comprehensive

## Conclusion

This file change matrix provides a comprehensive blueprint for implementing all critical fixes identified in the code review. The matrix prioritizes changes by risk and impact, ensuring that the most critical issues are addressed first while maintaining system stability.

**Key Considerations:**
1. **Dependencies:** Some files depend on others (e.g., interface changes require implementation updates)
2. **Testing:** Each file change requires corresponding test updates
3. **Documentation:** All changes must be documented for future maintenance
4. **Monitoring:** Critical files require enhanced monitoring during and after deployment

**Next Steps:**
1. Assign file ownership to team members
2. Create detailed implementation tickets
3. Set up monitoring for critical files
4. Begin implementation with highest priority files
