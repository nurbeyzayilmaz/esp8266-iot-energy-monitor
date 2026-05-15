"use client"

import { useState, useEffect } from 'react'
import { useRouter } from 'next/navigation'
import { Zap, Lock } from 'lucide-react'
import { getSettings } from '@/lib/supabase'

export default function LoginPage() {
  const router = useRouter()
  const [password, setPassword] = useState('')
  const [error, setError] = useState('')
  const [loading, setLoading] = useState(false)

  useEffect(() => {
    if (typeof window !== 'undefined' && localStorage.getItem('enerji_auth') === 'true') {
      router.push('/dashboard')
    }
  }, [router])

  async function handleLogin(e: React.FormEvent) {
    e.preventDefault()
    setLoading(true)
    setError('')
    try {
      const settings = await getSettings()
      if (password === settings['dashboard_password']) {
        localStorage.setItem('enerji_auth', 'true')
        router.push('/dashboard')
      } else {
        setError('Hatalı şifre. Tekrar deneyin.')
      }
    } catch {
      setError('Bağlantı hatası. Tekrar deneyin.')
    } finally {
      setLoading(false)
    }
  }

  return (
    <div className="min-h-screen flex items-center justify-center bg-gradient-to-br from-slate-900 to-slate-800 p-4">
      <div className="w-full max-w-sm">
        <div className="bg-slate-800 border border-slate-700 rounded-2xl p-8 shadow-2xl">
          <div className="flex flex-col items-center mb-8">
            <div className="bg-yellow-400/10 rounded-full p-4 mb-4">
              <Zap className="w-10 h-10 text-yellow-400" />
            </div>
            <h1 className="text-2xl font-bold text-white">Enerji Takip</h1>
            <p className="text-slate-400 text-sm mt-1">PZEM-004T İzleme Paneli</p>
          </div>

          <form onSubmit={handleLogin} className="flex flex-col gap-4">
            <div className="relative">
              <Lock className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-slate-400" />
              <input
                type="password"
                placeholder="Şifre"
                value={password}
                onChange={(e) => setPassword(e.target.value)}
                className="w-full bg-slate-700 border border-slate-600 rounded-lg py-3 pl-10 pr-4 text-white placeholder-slate-400 focus:outline-none focus:border-yellow-400 focus:ring-1 focus:ring-yellow-400 transition-colors"
                autoFocus
              />
            </div>

            {error && (
              <p className="text-red-400 text-sm text-center">{error}</p>
            )}

            <button
              type="submit"
              disabled={loading || !password}
              className="w-full bg-yellow-400 hover:bg-yellow-300 disabled:bg-slate-600 disabled:text-slate-400 text-slate-900 font-semibold py-3 rounded-lg transition-colors"
            >
              {loading ? 'Giriş yapılıyor...' : 'Giriş Yap'}
            </button>
          </form>
        </div>
      </div>
    </div>
  )
}
