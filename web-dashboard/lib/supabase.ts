import { createClient } from '@supabase/supabase-js'

const supabaseUrl = process.env.NEXT_PUBLIC_SUPABASE_URL!
const supabaseAnonKey = process.env.NEXT_PUBLIC_SUPABASE_ANON_KEY!

export const supabase = createClient(supabaseUrl, supabaseAnonKey)

export async function sendCommand(command: string) {
  return supabase.from('commands').insert({ command })
}

export async function getSettings(): Promise<Record<string, string>> {
  const { data } = await supabase.from('settings').select('*')
  if (!data) return {}
  return data.reduce((acc: Record<string, string>, s) => {
    acc[s.key] = s.value
    return acc
  }, {})
}

export async function updateSetting(key: string, value: string) {
  return supabase
    .from('settings')
    .update({ value, updated_at: new Date().toISOString() })
    .eq('key', key)
}

export async function getLatestReading() {
  const { data } = await supabase
    .from('readings')
    .select('*')
    .order('created_at', { ascending: false })
    .limit(1)
    .maybeSingle()
  return data
}

export async function getLatestReadingAfter(afterIso: string) {
  const { data } = await supabase
    .from('readings')
    .select('*')
    .gt('created_at', afterIso)
    .order('created_at', { ascending: false })
    .limit(1)
    .maybeSingle()
  return data
}

export async function getRecentReadings(hours = 24) {
  const since = new Date(Date.now() - hours * 60 * 60 * 1000).toISOString()
  const { data } = await supabase
    .from('readings')
    .select('id, power, energy, voltage, created_at')
    .gte('created_at', since)
    .order('created_at', { ascending: true })
  return data ?? []
}

export async function getRecentAlerts(limit = 5) {
  const { data } = await supabase
    .from('alerts')
    .select('*')
    .order('created_at', { ascending: false })
    .limit(limit)
  return data ?? []
}

export function formatUptime(seconds: number | null): string {
  if (!seconds) return '—'
  const h = Math.floor(seconds / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  const s = seconds % 60
  return `${h}sa ${m}dk ${s}sn`
}

export function getAdvisory(
  currentPower: number | null,
  avgPower: number,
  olcumSayisi: number
): { text: string; color: string } {
  if (!currentPower || currentPower === 0) {
    return { text: 'Cihaz bağlı değil veya röle açık', color: '#808080' }
  }
  if (olcumSayisi < 10) {
    return { text: `Veri toplanıyor... (${olcumSayisi}/10)`, color: '#808080' }
  }
  if (currentPower < 1.0) {
    return { text: 'Cihaz bağlı değil. Lütfen bir cihaz bağlayın.', color: '#808080' }
  }
  if (currentPower < 5.0) {
    return {
      text: `Verimli! Minimum tüketim: ${currentPower.toFixed(1)}W (Ort: ${avgPower.toFixed(0)}W)`,
      color: '#00CC00',
    }
  }
  if (currentPower < 25.0) {
    return {
      text: `Normal tüketim: ${currentPower.toFixed(1)}W (Ort: ${avgPower.toFixed(0)}W)`,
      color: '#0066FF',
    }
  }
  const yuzde = avgPower > 0 ? Math.round(((currentPower - avgPower) / avgPower) * 100) : 0
  return {
    text: `Dikkat! Yüksek tüketim: ${currentPower.toFixed(1)}W (Ort: ${avgPower.toFixed(0)}W, %${yuzde} fazla)`,
    color: '#FF0000',
  }
}
