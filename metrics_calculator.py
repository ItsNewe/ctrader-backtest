"""
Financial Metrics Calculator for Backtest Results
Calculates Sharpe Ratio, Calmar Ratio, Max Drawdown, and other risk/reward metrics
"""

import math
import statistics
from typing import List, Dict, Tuple
from dataclasses import dataclass
from datetime import datetime

@dataclass
class BacktestMetrics:
    """Container for all calculated metrics"""
    total_trades: int
    profit_loss: float
    win_rate: float
    max_drawdown: float
    max_drawdown_percent: float
    sharpe_ratio: float
    calmar_ratio: float
    recovery_factor: float
    profit_factor: float
    consecutive_wins: int
    consecutive_losses: int
    average_win: float
    average_loss: float
    expectancy: float
    std_deviation: float
    sortino_ratio: float
    return_percent: float
    margin_used_percent: float = 0.0  # % of account used as margin
    max_margin_used_percent: float = 0.0  # Peak margin usage %
    
    def to_dict(self) -> dict:
        """Convert to dictionary for JSON serialization"""
        return {
            'total_trades': self.total_trades,
            'profit_loss': round(self.profit_loss, 2),
            'win_rate': round(self.win_rate, 2),
            'max_drawdown': round(self.max_drawdown, 2),
            'max_drawdown_percent': round(self.max_drawdown_percent, 2),
            'sharpe_ratio': round(self.sharpe_ratio, 4),
            'calmar_ratio': round(self.calmar_ratio, 4),
            'recovery_factor': round(self.recovery_factor, 4),
            'profit_factor': round(self.profit_factor, 4),
            'consecutive_wins': self.consecutive_wins,
            'consecutive_losses': self.consecutive_losses,
            'average_win': round(self.average_win, 2),
            'average_loss': round(self.average_loss, 2),
            'expectancy': round(self.expectancy, 2),
            'std_deviation': round(self.std_deviation, 4),
            'sortino_ratio': round(self.sortino_ratio, 4),
            'return_percent': round(self.return_percent, 2),
            'margin_used_percent': round(self.margin_used_percent, 2),
            'max_margin_used_percent': round(self.max_margin_used_percent, 2),
            'sortino_ratio': round(self.sortino_ratio, 4),
            'return_percent': round(self.return_percent, 2),
        }


class MetricsCalculator:
    """Calculate comprehensive financial metrics from backtest results"""
    
    RISK_FREE_RATE = 0.02  # 2% annual risk-free rate
    
    @staticmethod
    def calculate_all_metrics(
        trades: List[Dict],
        initial_balance: float,
        daily_returns: List[float] = None
    ) -> BacktestMetrics:
        """
        Calculate all metrics from trade list
        
        Args:
            trades: List of trade dicts with 'profit' key
            initial_balance: Starting account balance
            daily_returns: Optional daily returns list for Sharpe/Sortino
        
        Returns:
            BacktestMetrics object with all calculations
        """
        if not trades:
            return MetricsCalculator._empty_metrics()
        
        profits = [t.get('profit', 0) for t in trades]
        total_profit = sum(profits)
        
        # Basic metrics
        total_trades = len(trades)
        wins = [p for p in profits if p > 0]
        losses = [p for p in profits if p < 0]
        win_count = len(wins)
        loss_count = len(losses)
        win_rate = (win_count / total_trades * 100) if total_trades > 0 else 0
        
        # Average win/loss
        average_win = (sum(wins) / len(wins)) if wins else 0
        average_loss = (sum(losses) / len(losses)) if losses else 0
        
        # Expectancy: average profit per trade
        expectancy = (total_profit / total_trades) if total_trades > 0 else 0
        
        # Profit factor: gross profit / gross loss
        gross_profit = sum(wins) if wins else 0
        gross_loss = abs(sum(losses)) if losses else 1
        profit_factor = (gross_profit / gross_loss) if gross_loss > 0 else 0
        
        # Consecutive wins/losses
        consecutive_wins = MetricsCalculator._max_consecutive(profits, positive=True)
        consecutive_losses = MetricsCalculator._max_consecutive(profits, positive=False)
        
        # Drawdown metrics
        max_dd, max_dd_pct, balance_curve = MetricsCalculator._calculate_drawdown(
            profits, initial_balance
        )
        
        # Return percent
        return_percent = (total_profit / initial_balance * 100) if initial_balance > 0 else 0
        
        # Recovery factor: total profit / max drawdown
        recovery_factor = (total_profit / max_dd) if max_dd > 0 else 0
        
        # Standard deviation of returns
        std_dev = MetricsCalculator._calculate_std_dev(profits)
        
        # Sharpe and Sortino ratios
        if daily_returns is None:
            daily_returns = MetricsCalculator._estimate_daily_returns(profits, total_trades)
        
        sharpe = MetricsCalculator._calculate_sharpe_ratio(daily_returns)
        sortino = MetricsCalculator._calculate_sortino_ratio(daily_returns)
        
        # Calmar ratio: annual return / max drawdown
        annual_return = return_percent * (252 / max(total_trades, 1))  # Assume ~252 trading days/year
        calmar = (annual_return / max(abs(max_dd_pct), 0.01)) if max_dd_pct > 0 else 0
        
        return BacktestMetrics(
            total_trades=total_trades,
            profit_loss=total_profit,
            win_rate=win_rate,
            max_drawdown=max_dd,
            max_drawdown_percent=max_dd_pct,
            sharpe_ratio=sharpe,
            calmar_ratio=calmar,
            recovery_factor=recovery_factor,
            profit_factor=profit_factor,
            consecutive_wins=consecutive_wins,
            consecutive_losses=consecutive_losses,
            average_win=average_win,
            average_loss=average_loss,
            expectancy=expectancy,
            std_deviation=std_dev,
            sortino_ratio=sortino,
            return_percent=return_percent,
        )
    
    @staticmethod
    def _calculate_drawdown(profits: List[float], initial_balance: float) -> Tuple[float, float, List[float]]:
        """Calculate maximum drawdown and drawdown percentage"""
        balance_curve = []
        current_balance = initial_balance
        
        peak_balance = initial_balance
        max_drawdown = 0
        
        for profit in profits:
            current_balance += profit
            balance_curve.append(current_balance)
            
            if current_balance > peak_balance:
                peak_balance = current_balance
            
            drawdown = peak_balance - current_balance
            max_drawdown = max(max_drawdown, drawdown)
        
        max_dd_percent = (max_drawdown / peak_balance * 100) if peak_balance > 0 else 0
        
        return max_drawdown, max_dd_percent, balance_curve
    
    @staticmethod
    def _max_consecutive(profits: List[float], positive: bool = True) -> int:
        """Calculate max consecutive wins or losses"""
        max_consecutive = 0
        current_consecutive = 0
        
        for profit in profits:
            is_positive = profit > 0
            
            if (positive and is_positive) or (not positive and not is_positive):
                current_consecutive += 1
                max_consecutive = max(max_consecutive, current_consecutive)
            else:
                current_consecutive = 0
        
        return max_consecutive
    
    @staticmethod
    def _calculate_std_dev(profits: List[float]) -> float:
        """Calculate standard deviation of returns"""
        if len(profits) < 2:
            return 0
        
        try:
            return statistics.stdev(profits)
        except (ValueError, statistics.StatisticsError):
            return 0
    
    @staticmethod
    def _estimate_daily_returns(profits: List[float], total_trades: int) -> List[float]:
        """Estimate daily returns from trade profits (assuming ~5 trades per day)"""
        if total_trades == 0:
            return [0]
        
        trades_per_day = max(total_trades / 252, 1)  # 252 trading days per year
        daily_returns = []
        
        for profit in profits:
            daily_return = profit / trades_per_day if trades_per_day > 0 else profit
            daily_returns.append(daily_return)
        
        return daily_returns if daily_returns else [0]
    
    @staticmethod
    def _calculate_sharpe_ratio(daily_returns: List[float]) -> float:
        """
        Calculate Sharpe Ratio
        Sharpe = (avg_return - risk_free_rate) / std_dev
        """
        if not daily_returns or len(daily_returns) < 2:
            return 0
        
        avg_return = sum(daily_returns) / len(daily_returns)
        daily_rf_rate = MetricsCalculator.RISK_FREE_RATE / 252  # Annualized to daily
        
        try:
            std_dev = statistics.stdev(daily_returns)
            if std_dev == 0:
                return 0
            sharpe = (avg_return - daily_rf_rate) / std_dev * math.sqrt(252)  # Annualize
            return sharpe
        except (ValueError, statistics.StatisticsError):
            return 0
    
    @staticmethod
    def _calculate_sortino_ratio(daily_returns: List[float]) -> float:
        """
        Calculate Sortino Ratio (only penalizes downside volatility)
        Sortino = (avg_return - risk_free_rate) / downside_std_dev
        """
        if not daily_returns or len(daily_returns) < 2:
            return 0
        
        avg_return = sum(daily_returns) / len(daily_returns)
        daily_rf_rate = MetricsCalculator.RISK_FREE_RATE / 252
        
        # Calculate downside deviation (only negative returns)
        downside_returns = [r for r in daily_returns if r < daily_rf_rate]
        
        if not downside_returns:
            downside_std = 0
        else:
            try:
                downside_std = statistics.stdev(downside_returns)
            except (ValueError, statistics.StatisticsError):
                downside_std = 0
        
        if downside_std == 0:
            return 0
        
        sortino = (avg_return - daily_rf_rate) / downside_std * math.sqrt(252)
        return sortino
    
    @staticmethod
    def _empty_metrics() -> BacktestMetrics:
        """Return empty metrics for failed backtests"""
        return BacktestMetrics(
            total_trades=0,
            profit_loss=0,
            win_rate=0,
            max_drawdown=0,
            max_drawdown_percent=0,
            sharpe_ratio=0,
            calmar_ratio=0,
            recovery_factor=0,
            profit_factor=0,
            consecutive_wins=0,
            consecutive_losses=0,
            average_win=0,
            average_loss=0,
            expectancy=0,
            std_deviation=0,
            sortino_ratio=0,
            return_percent=0,
        )
    
    @staticmethod
    def rank_strategies(results: List[Dict]) -> List[Dict]:
        """
        Rank strategies by composite score
        Prioritizes: Sharpe Ratio (60%), Calmar Ratio (25%), Win Rate (15%)
        """
        scored_results = []
        
        for result in results:
            metrics = result.get('metrics', {})
            sharpe = metrics.get('sharpe_ratio', 0)
            calmar = metrics.get('calmar_ratio', 0)
            win_rate = metrics.get('win_rate', 0) / 100  # Normalize to 0-1
            
            # Composite score (Sharpe weighted most, as it's most reliable)
            # Normalize to 0-100 scale
            score = (
                (sharpe / 3.0 * 60) +  # Sharpe usually -1 to 3, normalize and weight 60%
                (calmar / 3.0 * 25) +  # Calmar usually 0-3, normalize and weight 25%
                (win_rate * 15)         # Win rate 0-1, weight 15%
            )
            
            scored_results.append({
                **result,
                'composite_score': max(0, min(100, score))  # Clamp 0-100
            })
        
        # Sort by composite score descending
        scored_results.sort(key=lambda x: x['composite_score'], reverse=True)
        
        return scored_results
