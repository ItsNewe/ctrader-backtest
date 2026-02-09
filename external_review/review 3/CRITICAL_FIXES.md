# Critical Issues - Immediate Action Required

## Priority 1: Memory Management (Blocker)

### Issue: Memory Leaks in Strategy Cloning
**Status:** Critical - Must fix immediately
**Impact:** Application will eventually crash due to memory exhaustion

**Problem Code:**
```cpp
// src/main.cpp lines 56, 144, 238
IStrategy* Clone() const override {
  return new MACrossoverStrategy(params_);  // Memory leak!
}
```

**Solution - Option A (Smart Pointers):**
```cpp
// Modify IStrategy interface
class IStrategy {
public:
  virtual ~IStrategy() = default;
  virtual std::unique_ptr<IStrategy> Clone() const = 0;
  // ... other methods
};

// Implementation
std::unique_ptr<IStrategy> MACrossoverStrategy::Clone() const override {
  return std::make_unique<MACrossoverStrategy>(params_);
}
```

**Solution - Option B (RAII Strategy Manager):**
```cpp
class StrategyManager {
private:
  std::vector<std::unique_ptr<IStrategy>> strategies_;

public:
  IStrategy* CreateStrategy(StrategyType type, StrategyParams* params) {
    std::unique_ptr<IStrategy> strategy;
    switch (type) {
      case StrategyType::MA_CROSSOVER:
        strategy = std::make_unique<MACrossoverStrategy>(params);
        break;
      // ... other strategies
    }

    IStrategy* raw_ptr = strategy.get();
    strategies_.push_back(std::move(strategy));
    return raw_ptr;
  }
};
```

## Priority 2: Randomization (Data Integrity)

### Issue: Unseeded Random Number Generator
**Status:** Critical - Invalidates all backtest results
**Impact:** Research results are meaningless, trading decisions based on flawed data

**Problem Code:**
```cpp
int SimulateSlippage(bool is_buy, const BacktestConfig& config) const {
  int slippage = rand() % (config.max_slippage_points + 1);  // Always same sequence!
  return is_buy ? slippage : -slippage;
}
```

**Solution:**
```cpp
// random_generator.h
#include <random>

class RandomGenerator {
private:
  std::mt19937 generator_;

public:
  explicit RandomGenerator(unsigned int seed = std::random_device{}())
    : generator_(seed) {}

  int GenerateSlippage(int max_slippage_points) {
    std::uniform_int_distribution<> dist(0, max_slippage_points);
    return dist(generator_);
  }

  // For testing - allow fixed seed
  void SetSeed(unsigned int seed) {
    generator_.seed(seed);
  }
};

// Modify BacktestEngine
class BacktestEngine {
private:
  RandomGenerator random_gen_;

public:
  BacktestEngine(const BacktestConfig& config, unsigned int random_seed = std::random_device{}())
    : config_(config), random_gen_(random_seed) {}

  int SimulateSlippage(bool is_buy, const BacktestConfig& config) const {
    int slippage = random_gen_.GenerateSlippage(config.max_slippage_points);
    return is_buy ? slippage : -slippage;
  }
};
```

## Priority 3: Security Vulnerabilities

### Issue: SQL Injection in Sweep Endpoints
**Status:** Critical - Remote code execution possible
**Impact:** Complete system compromise

**Problem Code:**
```python
# server.py - vulnerable
cursor.execute('SELECT sweep_id, created_at, status FROM sweeps WHERE id = ' + user_input)
```

**Solution:**
```python
# server.py - secure
@app.route('/api/sweep/<sweep_id>', methods=['GET'])
def get_sweep_results(sweep_id):
    try:
        # Validate sweep_id format (only alphanumeric + underscore/hyphen)
        if not re.match(r'^[a-zA-Z0-9_-]+$', sweep_id):
            return jsonify({'status': 'error', 'message': 'Invalid sweep ID'}), 400

        executor = SweepExecutor()
        conn = sqlite3.connect(str(executor.results_db))
        cursor = conn.cursor()

        cursor.execute('''
            SELECT sweep_id, created_at, status, total_combinations, completed_combinations
            FROM sweeps
            WHERE sweep_id = ?
        ''', (sweep_id,))

        result = cursor.fetchone()
        conn.close()

        if not result:
            return jsonify({'status': 'error', 'message': 'Sweep not found'}), 404

        return jsonify(dict(zip(['sweep_id', 'created_at', 'status', 'total', 'completed'], result))), 200

    except Exception as e:
        logger.error(f"Error fetching sweep {sweep_id}: {str(e)}")
        return jsonify({'status': 'error', 'message': 'Internal server error'}), 500
```

### Issue: Path Traversal Vulnerability
**Status:** Critical - File system access outside allowed directories

**Problem Code:**
```python
data_file = request.json.get('data_file')  # "../../../etc/passwd"
with open(data_file, 'r') as f:  # Accesses any file!
```

**Solution:**
```python
import os
from pathlib import Path

class PathValidator:
    ALLOWED_DATA_DIR = Path('data')
    ALLOWED_EXTENSIONS = {'.csv', '.txt', '.json'}

    @classmethod
    def validate_and_resolve(cls, filename: str) -> Path:
        """Securely validate and resolve file paths."""
        if not filename:
            raise ValueError("Filename cannot be empty")

        # Basic sanitization
        if '..' in filename or filename.startswith('/') or '\\' in filename:
            raise ValueError("Invalid filename characters")

        # Check extension
        file_ext = Path(filename).suffix.lower()
        if file_ext not in cls.ALLOWED_EXTENSIONS:
            raise ValueError(f"File type not allowed: {file_ext}")

        # Resolve path and check it's within allowed directory
        full_path = (cls.ALLOWED_DATA_DIR / filename).resolve()

        if not full_path.is_relative_to(cls.ALLOWED_DATA_DIR):
            raise ValueError("Access denied: path outside allowed directory")

        if not full_path.exists():
            raise FileNotFoundError(f"Data file not found: {filename}")

        return full_path

# Usage in Flask endpoint
@app.route('/api/backtest/run', methods=['POST'])
def run_backtest():
    try:
        config = request.get_json()

        # Validate data file path
        data_file = PathValidator.validate_and_resolve(config['data_file'])

        # Proceed with validated path
        # ...

    except ValueError as e:
        return jsonify({'status': 'error', 'message': str(e)}), 400
    except FileNotFoundError as e:
        return jsonify({'status': 'error', 'message': str(e)}), 404
```

### Issue: Plain Text Credential Storage
**Status:** Critical - Credentials exposed in logs/source code

**Solution:**
```python
# secure_config.py
import os
import base64
from cryptography.fernet import Fernet, InvalidToken
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.kdf.pbkdf2 import PBKDF2HMAC

class SecureConfig:
    def __init__(self):
        # Get encryption key from environment
        key_env = os.environ.get('BACKTEST_ENCRYPTION_KEY')
        if not key_env:
            raise ValueError("BACKTEST_ENCRYPTION_KEY environment variable required")

        # Derive key from password using PBKDF2
        salt = b'backtest_salt_2024'  # In production, use unique salt per installation
        kdf = PBKDF2HMAC(
            algorithm=hashes.SHA256(),
            length=32,
            salt=salt,
            iterations=100000,
        )
        key = base64.urlsafe_b64encode(kdf.derive(key_env.encode()))
        self.cipher = Fernet(key)

    def encrypt_value(self, value: str) -> str:
        """Encrypt a sensitive value."""
        if not value:
            return ""
        return self.cipher.encrypt(value.encode()).decode()

    def decrypt_value(self, encrypted: str) -> str:
        """Decrypt a sensitive value."""
        if not encrypted:
            return ""
        try:
            return self.cipher.decrypt(encrypted.encode()).decode()
        except InvalidToken:
            raise ValueError("Failed to decrypt value - invalid key or corrupted data")

# Secure broker account
@dataclass
class SecureBrokerAccount:
    broker: str
    account_id: str
    encrypted_api_key: str
    encrypted_api_secret: str
    encrypted_password: str = ""

    def get_credentials(self, config: SecureConfig) -> dict:
        """Get decrypted credentials."""
        return {
            'api_key': config.decrypt_value(self.encrypted_api_key),
            'api_secret': config.decrypt_value(self.encrypted_api_secret),
            'password': config.decrypt_value(self.encrypted_password) if self.encrypted_password else ""
        }

# Migration script to encrypt existing credentials
def migrate_credentials():
    """One-time migration to encrypt existing plain text credentials."""
    config = SecureConfig()

    # Read existing config
    with open('broker_config.json', 'r') as f:
        old_config = json.load(f)

    # Encrypt sensitive fields
    for account in old_config['accounts']:
        if 'api_key' in account and not account['api_key'].startswith('gAAAAA'):  # Not already encrypted
            account['encrypted_api_key'] = config.encrypt_value(account.pop('api_key'))
            account['encrypted_api_secret'] = config.encrypt_value(account.pop('api_secret', ''))

    # Save encrypted config
    with open('secure_broker_config.json', 'w') as f:
        json.dump(old_config, f, indent=2)

    print("Credentials encrypted successfully. Delete the old config file.")
```

## Priority 4: Server Stability

### Issue: Blocking Operations in Flask Routes
**Status:** Critical - Server becomes unresponsive

**Problem Code:**
```python
@app.route('/api/backtest/run', methods=['POST'])
def run_backtest():
    result = run_backtest_sync(config)  # Blocks entire server!
    return jsonify(result)
```

**Solution:**
```python
from concurrent.futures import ThreadPoolExecutor
import uuid

# Global thread pool
executor = ThreadPoolExecutor(max_workers=4)
backtest_futures = {}

@app.route('/api/backtest/run', methods=['POST'])
def run_backtest():
    try:
        config = request.get_json()

        # Validate config first (fast operation)
        errors = validate_backtest_config(config)
        if errors:
            return jsonify({'status': 'error', 'message': 'Validation failed', 'errors': errors}), 400

        # Submit to thread pool
        backtest_id = str(uuid.uuid4())
        future = executor.submit(run_backtest_async, config)
        backtest_futures[backtest_id] = future

        return jsonify({
            'status': 'running',
            'backtest_id': backtest_id,
            'message': 'Backtest started successfully'
        }), 202

    except Exception as e:
        logger.error(f"Backtest submission error: {str(e)}", exc_info=True)
        return jsonify({'status': 'error', 'message': 'Failed to start backtest'}), 500

@app.route('/api/backtest/status/<backtest_id>', methods=['GET'])
def get_backtest_status(backtest_id):
    if backtest_id not in backtest_futures:
        return jsonify({'status': 'error', 'message': 'Backtest not found'}), 404

    future = backtest_futures[backtest_id]

    if future.done():
        try:
            result = future.result()
            del backtest_futures[backtest_id]  # Cleanup
            return jsonify({'status': 'completed', 'result': result})
        except Exception as e:
            logger.error(f"Backtest {backtest_id} failed: {str(e)}")
            del backtest_futures[backtest_id]  # Cleanup
            return jsonify({'status': 'error', 'error': str(e)}), 500
    else:
        return jsonify({'status': 'running'}), 202

def run_backtest_async(config):
    """Run backtest in background thread."""
    try:
        logger.info(f"Starting backtest for config: {config.get('strategy', 'unknown')}")

        # Actual backtest execution
        result = run_backtest_sync(config)

        logger.info(f"Backtest completed for {config.get('strategy', 'unknown')}")
        return result

    except Exception as e:
        logger.error(f"Backtest execution failed: {str(e)}", exc_info=True)
        raise
```

## Priority 5: Input Validation

### Issue: No Input Validation in API Endpoints
**Status:** Critical - Potential for various injection attacks

**Solution:**
```python
# validation.py
from pydantic import BaseModel, validator, ValidationError
from typing import Optional, List
from datetime import datetime
import re

class BacktestConfig(BaseModel):
    strategy: str
    data_file: str
    start_date: str
    end_date: str
    testing_mode: str
    lot_size: float
    stop_loss_pips: Optional[float] = None
    take_profit_pips: Optional[float] = None
    spread_pips: float = 2.0

    @validator('strategy')
    def validate_strategy(cls, v):
        allowed = ['ma_crossover', 'breakout', 'scalping', 'grid']
        if v not in allowed:
            raise ValueError(f'Strategy must be one of: {allowed}')
        return v

    @validator('testing_mode')
    def validate_testing_mode(cls, v):
        allowed = ['bar', 'tick']
        if v not in allowed:
            raise ValueError(f'Testing mode must be one of: {allowed}')
        return v

    @validator('lot_size')
    def validate_lot_size(cls, v):
        if v <= 0:
            raise ValueError('Lot size must be positive')
        if v > 100.0:
            raise ValueError('Lot size cannot exceed 100.0')
        return v

    @validator('data_file')
    def validate_data_file(cls, v):
        if not v:
            raise ValueError('Data file is required')

        # Check for path traversal
        if '..' in v or v.startswith('/') or '\\' in v:
            raise ValueError('Invalid data file path')

        # Check extension
        if not v.lower().endswith('.csv'):
            raise ValueError('Data file must be a CSV file')

        return v

    @validator('start_date', 'end_date')
    def validate_date(cls, v):
        try:
            datetime.fromisoformat(v.replace('Z', '+00:00'))
            return v
        except ValueError:
            raise ValueError('Invalid date format. Use ISO format (YYYY-MM-DDTHH:MM:SS)')

    @validator('start_date', 'end_date', pre=True, always=True)
    def check_date_order(cls, v, values, field):
        if field.name == 'end_date' and 'start_date' in values:
            start = datetime.fromisoformat(values['start_date'].replace('Z', '+00:00'))
            end = datetime.fromisoformat(v.replace('Z', '+00:00'))
            if start >= end:
                raise ValueError('End date must be after start date')
        return v

class BrokerConnectRequest(BaseModel):
    broker: str
    account_type: str
    account_id: str
    leverage: int
    account_currency: str
    api_key: Optional[str] = None
    api_secret: Optional[str] = None
    mt5_path: Optional[str] = None

    @validator('broker')
    def validate_broker(cls, v):
        allowed = ['ctrader', 'metatrader5', 'mt5']
        if v.lower() not in allowed:
            raise ValueError(f'Broker must be one of: {allowed}')
        return v.lower()

    @validator('account_type')
    def validate_account_type(cls, v):
        allowed = ['demo', 'live']
        if v.lower() not in allowed:
            raise ValueError(f'Account type must be one of: {allowed}')
        return v.lower()

    @validator('leverage')
    def validate_leverage(cls, v):
        if v < 1 or v > 1000:
            raise ValueError('Leverage must be between 1 and 1000')
        return v

# Flask endpoint with validation
@app.route('/api/backtest/run', methods=['POST'])
def run_backtest():
    try:
        data = request.get_json()
        config = BacktestConfig(**data)
        # Validation passed - proceed with backtest
        return start_backtest(config.dict())
    except ValidationError as e:
        return jsonify({
            'status': 'error',
            'message': 'Validation failed',
            'errors': e.errors()
        }), 400

@app.route('/api/broker/connect', methods=['POST'])
def connect_broker():
    try:
        data = request.get_json()
        broker_req = BrokerConnectRequest(**data)
        # Validation passed - proceed with connection
        return connect_to_broker(broker_req.dict())
    except ValidationError as e:
        return jsonify({
            'status': 'error',
            'message': 'Validation failed',
            'errors': e.errors()
        }), 400
```

## Testing Critical Fixes

Before deploying any critical fixes, implement these tests:

```python
# test_critical_fixes.py
import pytest
import tempfile
import os
from pathlib import Path

def test_path_validation():
    """Test path traversal protection."""
    validator = PathValidator()

    # Valid paths
    assert validator.validate_and_resolve('valid_file.csv')

    # Invalid paths
    with pytest.raises(ValueError, match="Invalid filename"):
        validator.validate_and_resolve('../../../etc/passwd')

    with pytest.raises(ValueError, match="File type not allowed"):
        validator.validate_and_resolve('script.py')

def test_sql_injection_protection():
    """Test SQL injection prevention."""
    # This should work
    result = get_sweep_results('valid_sweep_id_123')

    # This should fail safely
    with pytest.raises(ValueError):
        get_sweep_results("'; DROP TABLE sweeps; --")

def test_memory_leak_prevention():
    """Test that strategy cloning doesn't leak memory."""
    import gc
    import psutil

    initial_memory = psutil.Process().memory_info().rss

    # Create and clone many strategies
    for i in range(1000):
        strategy = MACrossoverStrategy(params)
        cloned = strategy.clone()  # Should use smart pointers
        # cloned goes out of scope, memory should be freed

    gc.collect()
    final_memory = psutil.Process().memory_info().rss

    # Memory should not have grown significantly
    assert final_memory - initial_memory < 10 * 1024 * 1024  # Less than 10MB growth

def test_random_reproducibility():
    """Test that random seeding works correctly."""
    gen1 = RandomGenerator(12345)
    gen2 = RandomGenerator(12345)

    # Same seed should produce same sequence
    for i in range(100):
        assert gen1.generate_slippage(10) == gen2.generate_slippage(10)

    # Different seeds should produce different sequences
    gen3 = RandomGenerator(54321)
    assert gen1.generate_slippage(10) != gen3.generate_slippage(10)
```

## Deployment Checklist

- [ ] All critical fixes implemented and tested
- [ ] Memory leak tests pass
- [ ] Random seeding validation complete
- [ ] Security vulnerability scans pass
- [ ] Input validation covers all endpoints
- [ ] Server stability tests pass under load
- [ ] Credentials encrypted and migration complete
- [ ] Code review by security team
- [ ] Penetration testing completed
- [ ] Rollback plan documented
- [ ] Monitoring alerts configured for new error patterns
