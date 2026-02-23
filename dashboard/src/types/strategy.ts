export interface StrategyParameter {
  name: string;
  label: string;
  type: 'float' | 'int' | 'bool' | 'select';
  default: number | string | boolean;
  min?: number;
  max?: number;
  step?: number;
  options?: string[];
  description: string;
}

export interface Strategy {
  id: string;
  name: string;
  description: string;
  cli_name: string;
  parameters: StrategyParameter[];
}
