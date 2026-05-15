"use client"

import { useState, useEffect, useCallback, useRef } from 'react'
import { useRouter } from 'next/navigation'
import {
  Zap, Activity, Gauge, PlugZap, Thermometer, Wifi,
  WifiOff, Power, RotateCcw, Settings, LogOut, AlertTriangle,
  CheckCircle, Radio, Clock
} from 'lucide-react'
import {
  LineChart, Line, XAxis, YAxis, CartesianGrid,
  Tooltip, ResponsiveContainer, ReferenceLine
} from 'recharts'
import {
  supabase, sendCommand, getSettings,
  getLatestReading, getLatestReadingAfter, getRecentReadings, formatUptime, getAdvisory
} from '@/lib/supabase'
import type { Reading } from '@/lib/types'

type ChartPoint = { time: string; power: number; full: string }
type ChartRange = '15dk' | '1sa' | '6sa' | '24sa'

export default function DashboardPage() {
  const router = useRouter()
  const [latest, setLatest] = useState<Reading | null>(null)
  const [allReadings, setAllReadings] = useState<ChartPoint[]>([])
  const [chartRange, setChartRange] = useState<ChartRange>('1sa')
  const [settings, setSettings] = useState<Record<string, string>>({})
  const [relayOn, setRelayOn] = useState(false)
  const [avgPower, setAvgPower] = useState(0)
  const [olcumSayisi, setOlcumSayisi] = useState(0)
  const [cmdLoading, setCmdLoading] = useState(false)
  const [lastUpdate, setLastUpdate] = useState<Date | null>(null)
  const [dataAge, setDataAge] = useState(0)
  const [refreshInterval, setRefreshInterval] = useState(1)
  const [realtimeOk, setRealtimeOk] = useState(false)
  const [pulse, setPulse] = useState(false)
  const pollingRef = useRef<ReturnType<typeof setInterval> | null>(null)
  const ageRef = useRef<ReturnType<typeof setInterval> | null>(null)
  const lastCreatedRef = useRef<string>('')

  // Veri tazeliği: kaç saniye önce güncellendi
  const isOnline = dataAge < 15
  const isStale   = dataAge >= 15 && dataAge < 60

  // Grafik verisi: seçilen aralığa göre filtrele
  const rangeMs: Record<ChartRange, number> = {
    '15dk': 15 * 60 * 1000,
    '1sa':  60 * 60 * 1000,
    '6sa':  6  * 60 * 60 * 1000,
    '24sa': 24 * 60 * 60 * 1000,
  }
  const chartData = allReadings.filter(p => {
    const t = new Date(p.full).getTime()
    return Date.now() - t <= rangeMs[chartRange]
  })

  // Auth kontrolü
  useEffect(() => {
    if (typeof window === 'undefined') return
    if (localStorage.getItem('enerji_auth') !== 'true') router.push('/')
  }, [router])

  // Veri tazeliği sayacı (her saniye artar)
  useEffect(() => {
    if (ageRef.current) clearInterval(ageRef.current)
    ageRef.current = setInterval(() => {
      setDataAge(prev => prev + 1)
    }, 1000)
    return () => { if (ageRef.current) clearInterval(ageRef.current) }
  }, [])

  // Yeni veri gelince tazelik sayacını sıfırla + pulse animasyonu
  const applyReading = useCallback((r: Reading) => {
    lastCreatedRef.current = r.created_at
    setLatest(r)
    setLastUpdate(new Date(r.created_at))
    setDataAge(0)
    setPulse(true)
    setTimeout(() => setPulse(false), 400)
    const p = r.power ?? 0
    if (p > 0 && p < 4000) {
      setAvgPower(prev => prev === 0 ? p : prev * 0.7 + p * 0.3)
      setOlcumSayisi(prev => prev + 1)
    }
    setAllReadings(prev => {
      const point: ChartPoint = {
        time: new Date(r.created_at).toLocaleTimeString('tr-TR', { hour: '2-digit', minute: '2-digit' }),
        full: r.created_at,
        power: Number(p.toFixed(1)),
      }
      const next = [...prev, point]
      return next.length > 500 ? next.slice(-500) : next
    })
  }, [])

  // İlk yükleme
  const loadInitial = useCallback(async () => {
    const [s, r, recent] = await Promise.all([
      getSettings(),
      getLatestReading(),
      getRecentReadings(24),
    ])
    setSettings(s)
    setRelayOn(s['role_durumu'] === 'true')
    const interval = parseInt(s['yenileme_araligi'] ?? '2', 10)
    if (interval >= 1 && interval <= 60) setRefreshInterval(interval)
    if (r) applyReading(r)
    if (recent.length > 0) {
      setAllReadings(recent.map(x => ({
        time: new Date(x.created_at).toLocaleTimeString('tr-TR', { hour: '2-digit', minute: '2-digit' }),
        full: x.created_at,
        power: Number((x.power ?? 0).toFixed(1)),
      })))
      const powers = recent.map(x => x.power ?? 0).filter(p => p > 0 && p < 4000)
      if (powers.length > 0) {
        setAvgPower(powers.reduce((a, b) => a + b, 0) / powers.length)
        setOlcumSayisi(powers.length)
      }
    }
  }, [applyReading])

  useEffect(() => { loadInitial() }, [loadInitial])

  // Polling — sadece son veri tarihinden SONRA yeni satır varsa güncelle
  const fetchLatest = useCallback(async () => {
    if (lastCreatedRef.current) {
      const r = await getLatestReadingAfter(lastCreatedRef.current)
      if (r) {
        lastCreatedRef.current = r.created_at
        applyReading(r)
      }
    } else {
      const r = await getLatestReading()
      if (r) {
        lastCreatedRef.current = r.created_at
        applyReading(r)
      }
    }
  }, [applyReading])

  // Supabase Realtime — otomatik yeniden baglantili
  useEffect(() => {
    let channel = supabase
      .channel('readings-live')
      .on('postgres_changes',
        { event: 'INSERT', schema: 'public', table: 'readings' },
        (payload) => { applyReading(payload.new as Reading) }
      )
      .subscribe((status) => {
        setRealtimeOk(status === 'SUBSCRIBED')
        if (status === 'CHANNEL_ERROR' || status === 'TIMED_OUT') {
          setRealtimeOk(false)
          setTimeout(() => {
            supabase.removeChannel(channel)
            channel = supabase
              .channel('readings-live-retry-' + Date.now())
              .on('postgres_changes',
                { event: 'INSERT', schema: 'public', table: 'readings' },
                (payload) => { applyReading(payload.new as Reading) }
              )
              .subscribe((s) => setRealtimeOk(s === 'SUBSCRIBED'))
          }, 3000)
        }
      })
    return () => { supabase.removeChannel(channel) }
  }, [applyReading])

  // Page Visibility API: sekme aktif olunca hemen guncelle
  useEffect(() => {
    const onVisible = () => {
      if (document.visibilityState === 'visible') fetchLatest()
    }
    document.addEventListener('visibilitychange', onVisible)
    return () => document.removeEventListener('visibilitychange', onVisible)
  }, [fetchLatest])

  useEffect(() => {
    if (pollingRef.current) clearInterval(pollingRef.current)
    pollingRef.current = setInterval(fetchLatest, refreshInterval * 1000)
    return () => { if (pollingRef.current) clearInterval(pollingRef.current) }
  }, [fetchLatest, refreshInterval])

  // Komutlar
  async function handleRelay() {
    setCmdLoading(true)
    const newRelay = !relayOn
    await sendCommand(newRelay ? 'relay_on' : 'relay_off')
    await supabase.from('settings').update({ value: String(newRelay) }).eq('key', 'role_durumu')
    setRelayOn(newRelay)
    setCmdLoading(false)
  }

  async function handleEnergyReset() {
    if (!confirm('Enerji sayacı sıfırlansın mı?')) return
    setCmdLoading(true)
    await sendCommand('energy_reset')
    setCmdLoading(false)
  }

  async function handleWifiReset() {
    if (!confirm('WiFi sıfırlanacak. ESP yeniden başlayacak. Devam?')) return
    setCmdLoading(true)
    await sendCommand('wifi_reset')
    setCmdLoading(false)
  }

  function handleLogout() {
    localStorage.removeItem('enerji_auth')
    router.push('/')
  }

  // Hesaplamalar
  const gucEsigi    = parseFloat(settings['guc_esigi']  ?? '100')
  const birimFiyat  = parseFloat(settings['birim_fiyat'] ?? '3.20')
  const energy      = latest?.energy ?? 0
  const fatura      = (energy * birimFiyat).toFixed(2)
  const currentPower = relayOn ? 0 : (latest?.power ?? 0)
  const advisory    = getAdvisory(currentPower, avgPower, olcumSayisi)
  const asiriTuketim = !relayOn && currentPower > gucEsigi

  const metrics = [
    { label: 'Voltaj',       value: relayOn ? '0.0'   : (latest?.voltage?.toFixed(1)      ?? '—'), unit: 'V',   icon: Zap,         color: 'text-yellow-400' },
    { label: 'Akım',         value: relayOn ? '0.000' : (latest?.current_a?.toFixed(3)    ?? '—'), unit: 'A',   icon: Activity,    color: 'text-blue-400' },
    { label: 'Güç',          value: relayOn ? '0.0'   : (latest?.power?.toFixed(1)        ?? '—'), unit: 'W',   icon: Gauge,       color: asiriTuketim ? 'text-red-400' : 'text-green-400' },
    { label: 'Enerji',       value:                      (latest?.energy?.toFixed(3)      ?? '—'), unit: 'kWh', icon: PlugZap,     color: 'text-purple-400' },
    { label: 'Güç Faktörü',  value: relayOn ? '—'     : (latest?.power_factor?.toFixed(2) ?? '—'), unit: 'PF',  icon: Thermometer, color: 'text-orange-400' },
    { label: 'Frekans',      value: relayOn ? '—'     : (latest?.frequency?.toFixed(1)    ?? '—'), unit: 'Hz',  icon: Activity,    color: 'text-cyan-400' },
    { label: 'Max Güç',      value:                      (latest?.max_power?.toFixed(1)   ?? '—'), unit: 'W',   icon: Gauge,       color: 'text-red-400' },
    { label: 'Est. Fatura',  value: fatura,                                                         unit: '₺',   icon: PlugZap,     color: 'text-emerald-400' },
  ]

  return (
    <div className="min-h-screen bg-slate-900 text-white">
      {/* Header */}
      <header className="bg-slate-800 border-b border-slate-700 px-4 py-3 flex items-center justify-between">
        <div className="flex items-center gap-2">
          <Zap className="w-6 h-6 text-yellow-400" />
          <span className="font-bold text-lg">Enerji Takip</span>
        </div>
        <div className="flex items-center gap-2 flex-wrap justify-end">
          {/* Realtime durumu */}
          <div className="flex items-center gap-1">
            <Radio className={`w-3.5 h-3.5 ${realtimeOk ? 'text-green-400' : 'text-slate-500'}`} />
            <span className={`text-xs ${realtimeOk ? 'text-green-400' : 'text-slate-500'}`}>
              {realtimeOk ? 'Canlı' : 'Polling'}
            </span>
          </div>
          {/* Veri tazeliği */}
          <div className={`flex items-center gap-1 rounded-full px-2 py-0.5 text-xs transition-colors ${
            isOnline ? 'bg-green-500/10 text-green-400' :
            isStale  ? 'bg-yellow-500/10 text-yellow-400' :
                       'bg-red-500/10 text-red-400'
          }`}>
            <Clock className="w-3 h-3" />
            {dataAge === 0 ? 'Şimdi' : `${dataAge}sn önce`}
          </div>
          {/* Cihaz durumu */}
          <div className="flex items-center gap-1 text-sm">
            {isOnline ? (
              <><Wifi className="w-4 h-4 text-green-400" /><span className="text-green-400 text-xs hidden sm:inline">Çevrimiçi</span></>
            ) : (
              <><WifiOff className="w-4 h-4 text-red-400" /><span className="text-red-400 text-xs hidden sm:inline">Çevrimdışı</span></>
            )}
          </div>
          {latest?.rssi && (
            <span className="text-slate-500 text-xs hidden sm:inline">{latest.rssi}dBm</span>
          )}
          <button onClick={() => router.push('/settings')} className="p-1.5 text-slate-400 hover:text-white transition-colors">
            <Settings className="w-5 h-5" />
          </button>
          <button onClick={handleLogout} className="p-1.5 text-slate-400 hover:text-white transition-colors">
            <LogOut className="w-5 h-5" />
          </button>
        </div>
      </header>

      <main className="max-w-5xl mx-auto p-4 space-y-4">
        {/* Aşırı tüketim uyarısı */}
        {asiriTuketim && (
          <div className="bg-red-500/20 border border-red-500 rounded-xl p-3 flex items-center gap-2 animate-pulse">
            <AlertTriangle className="w-5 h-5 text-red-400 flex-shrink-0" />
            <span className="text-red-300 text-sm font-medium">
              Aşırı tüketim! {currentPower.toFixed(0)}W — Eşik: {gucEsigi}W
            </span>
          </div>
        )}

        {/* Durum + Öneri */}
        <div className={`bg-slate-800 rounded-xl p-4 flex items-start gap-3 transition-all duration-300 ${pulse ? 'ring-1 ring-yellow-400/50' : ''}`}>
          {relayOn ? (
            <AlertTriangle className="w-5 h-5 text-orange-400 flex-shrink-0 mt-0.5" />
          ) : asiriTuketim ? (
            <AlertTriangle className="w-5 h-5 text-red-400 flex-shrink-0 mt-0.5" />
          ) : (
            <CheckCircle className="w-5 h-5 text-green-400 flex-shrink-0 mt-0.5" />
          )}
          <div className="flex-1 min-w-0">
            <p className="text-sm font-medium" style={{ color: relayOn ? '#FF6600' : advisory.color }}>
              {relayOn ? 'RÖLE AÇIK — Akım Kesildi' : advisory.text}
            </p>
            <div className="flex items-center gap-3 mt-0.5 flex-wrap">
              {latest && (
                <p className="text-slate-500 text-xs">
                  {new Date(latest.created_at).toLocaleTimeString('tr-TR')}
                  {latest.uptime_seconds ? ` • Uptime: ${formatUptime(latest.uptime_seconds)}` : ''}
                </p>
              )}
              {avgPower > 0 && (
                <p className="text-slate-500 text-xs">Ort: {avgPower.toFixed(1)}W</p>
              )}
            </div>
          </div>
        </div>

        {/* Metrik kartlar — pulse animasyonu yeni veri gelince */}
        <div className="grid grid-cols-2 sm:grid-cols-4 gap-3">
          {metrics.map((m) => (
            <div key={m.label} className={`bg-slate-800 rounded-xl p-4 transition-all duration-300 ${pulse ? 'ring-1 ring-slate-600' : ''}`}>
              <div className="flex items-center gap-1.5 mb-2">
                <m.icon className={`w-4 h-4 ${m.color}`} />
                <span className="text-slate-400 text-xs">{m.label}</span>
              </div>
              <p className="text-2xl font-bold text-white tabular-nums">{m.value}</p>
              <p className="text-slate-500 text-xs">{m.unit}</p>
            </div>
          ))}
        </div>

        {/* Röle Kontrolü */}
        <div className="bg-slate-800 rounded-xl p-4">
          <h2 className="text-sm text-slate-400 mb-3 font-medium">Röle Kontrolü</h2>
          <button
            onClick={handleRelay}
            disabled={cmdLoading}
            className={`w-full py-4 rounded-xl font-bold text-lg flex items-center justify-center gap-2 transition-all ${
              relayOn
                ? 'bg-red-500/20 border-2 border-red-500 text-red-400 hover:bg-red-500/30'
                : 'bg-green-500/20 border-2 border-green-500 text-green-400 hover:bg-green-500/30'
            } disabled:opacity-50`}
          >
            <Power className="w-6 h-6" />
            {cmdLoading ? 'Komut gönderiliyor...' : relayOn ? 'RÖLE AÇIK — Kapat (Akım Geçsin)' : 'RÖLE KAPALI — Aç (Akım Kes)'}
          </button>
        </div>

        {/* Grafik + Aralık Seçici */}
        <div className="bg-slate-800 rounded-xl p-4">
          <div className="flex items-center justify-between mb-3">
            <h2 className="text-sm text-slate-400 font-medium">Güç Grafiği (W)</h2>
            <div className="flex gap-1">
              {(['15dk','1sa','6sa','24sa'] as ChartRange[]).map(r => (
                <button
                  key={r}
                  onClick={() => setChartRange(r)}
                  className={`text-xs px-2 py-1 rounded-lg transition-colors ${
                    chartRange === r
                      ? 'bg-yellow-400 text-slate-900 font-semibold'
                      : 'bg-slate-700 text-slate-400 hover:text-white'
                  }`}
                >
                  {r}
                </button>
              ))}
            </div>
          </div>
          {chartData.length > 1 ? (
            <ResponsiveContainer width="100%" height={200}>
              <LineChart data={chartData} margin={{ top: 5, right: 5, bottom: 5, left: 0 }}>
                <CartesianGrid strokeDasharray="3 3" stroke="#334155" />
                <XAxis dataKey="time" tick={{ fill: '#64748b', fontSize: 10 }} interval="preserveStartEnd" />
                <YAxis tick={{ fill: '#64748b', fontSize: 10 }} />
                <Tooltip
                  contentStyle={{ backgroundColor: '#1e293b', border: '1px solid #334155', borderRadius: '8px' }}
                  labelStyle={{ color: '#94a3b8' }}
                  itemStyle={{ color: '#facc15' }}
                />
                {gucEsigi > 0 && (
                  <ReferenceLine y={gucEsigi} stroke="#ef4444" strokeDasharray="4 4" label={{ value: `Eşik ${gucEsigi}W`, fill: '#ef4444', fontSize: 10 }} />
                )}
                <Line type="monotone" dataKey="power" stroke="#facc15" strokeWidth={2} dot={false} name="Güç (W)" isAnimationActive={false} />
              </LineChart>
            </ResponsiveContainer>
          ) : (
            <div className="h-48 flex flex-col items-center justify-center text-slate-500 text-sm gap-2">
              <Activity className="w-8 h-8 text-slate-700" />
              Grafik için veri bekleniyor...
            </div>
          )}
        </div>

        {/* Alt butonlar */}
        <div className="grid grid-cols-2 gap-3 pb-4">
          <button onClick={handleEnergyReset} disabled={cmdLoading}
            className="bg-slate-800 border border-slate-700 hover:border-yellow-400 rounded-xl p-3 flex items-center justify-center gap-2 text-slate-300 hover:text-yellow-400 transition-colors text-sm disabled:opacity-50">
            <RotateCcw className="w-4 h-4" /> Enerji Sıfırla
          </button>
          <button onClick={handleWifiReset} disabled={cmdLoading}
            className="bg-slate-800 border border-slate-700 hover:border-red-400 rounded-xl p-3 flex items-center justify-center gap-2 text-slate-300 hover:text-red-400 transition-colors text-sm disabled:opacity-50">
            <WifiOff className="w-4 h-4" /> WiFi Sıfırla
          </button>
        </div>
      </main>
    </div>
  )
}
