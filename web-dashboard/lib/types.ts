export type Reading = {
  id: number
  voltage: number | null
  current_a: number | null
  power: number | null
  energy: number | null
  power_factor: number | null
  frequency: number | null
  rssi: number | null
  uptime_seconds: number | null
  max_power: number | null
  created_at: string
}

export type Command = {
  id: number
  command: string
  executed: boolean
  created_at: string
}

export type Setting = {
  key: string
  value: string
  updated_at: string
}

export type Alert = {
  id: number
  message: string
  power_value: number | null
  created_at: string
}

export type Settings = {
  guc_esigi: string
  birim_fiyat: string
  offset_guc: string
  dashboard_password: string
  role_durumu: string
}
