import { Component, OnInit } from '@angular/core';
import { CommonModule } from '@angular/common';
import { RouterModule } from '@angular/router';
import { Topbar } from '../../shared/topbar/topbar';
import { Database, ref, get, child } from '@angular/fire/database';
import { AuthService } from '../../services/auth.service';
import { FormsModule } from '@angular/forms';
import { Chart } from 'chart.js/auto';
import { getStorage, ref as storageRef, getDownloadURL, listAll, getMetadata } from '@angular/fire/storage';

@Component({
  selector: 'app-dashboard-root',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterModule, Topbar],
  templateUrl: './dashboard-root.html',
  styleUrls: ['./dashboard-root.css']
})
export class DashboardRoot implements OnInit {
  totalClientes = 0;
  totalUsuarios = 0;
  totalDispositivos = 0;
  dispositivosOffline = 0;
  dispositivosFiltrados: any[] = [];
  periodoInicio: string = '';
  periodoFim: string = '';
  dispositivoSelecionado: string = '';
  totalDispositivosPre = 0;
  fotosCapturadas: any[] = [];
  dispositivos: any[] = [];
  fotoSelecionada: number = 0;
  limiteFotos: number = 7;
  grafico!: Chart;
  imagemSelecionada: any = null;
  dadosImagemSelecionada: any = null;
  lineChartData: any;

  constructor(private db: Database, public auth: AuthService) { }

  ngOnInit(): void {
    if (this.auth.getRole() !== 'root') {
      this.carregarDados();
      this.carregarListaDispositivos();
    } else {
      this.carregarDados();
    }
  }

  async carregarDados() {
    const dbRef = ref(this.db);
    const role = this.auth.getRole();
    const clienteId = this.auth.getClienteId();

    try {
      if (role === 'root') {
        const snapshot = await get(child(dbRef, 'clientes'));
        if (snapshot.exists()) {
          const clientes = snapshot.val();
          const clienteIds = Object.keys(clientes);
          this.totalClientes = clienteIds.length;
          this.contabilizarDados(clientes, clienteIds);
          this.carregarDispositivosPre();
        }
      } else {
        if (!clienteId) return;
        const snapshot = await get(child(dbRef, `clientes/${clienteId}`));
        if (snapshot.exists()) {
          const cliente = snapshot.val();
          this.totalClientes = 1;
          this.contabilizarDados({ [clienteId]: cliente }, [clienteId]);
          this.carregarDispositivosPre();
        }
      }
      console.log('Valores enviados ao Topbar:', {
        totalClientes: this.totalClientes,
        totalDispositivos: this.totalDispositivos,
        totalUsuarios: this.totalUsuarios,
        totalDispositivosPre: this.totalDispositivosPre
      });

    } catch (error) {
      console.error('Erro ao carregar dados do dashboard:', error);
    }
  }

  async carregarListaDispositivos() {
    const dbRef = ref(this.db);
    try {
      const snapshot = await get(child(dbRef, `clientes/${this.auth.getClienteId()}/dispositivos`));
      if (snapshot.exists()) {
        const dados = snapshot.val();
        this.dispositivos = Object.keys(dados).map((key) => ({ uid: key, ...dados[key] }));
        this.dispositivoSelecionado = this.dispositivos[0]?.uid;
      }
    } catch (error) {
      console.error('Erro ao buscar dispositivos:', error);
    }
  }

  onDispositivoChange() {
    this.carregarFotosPorDispositivo(this.dispositivoSelecionado);
    this.carregarDadosGraficoPorDispositivo(this.dispositivoSelecionado);
  }

  parseTimestampToDate(timestamp: string): Date | null {
    if (!timestamp || timestamp.length < 15) return null;
    const year = +timestamp.slice(0, 4);
    const month = +timestamp.slice(4, 6) - 1;
    const day = +timestamp.slice(6, 8);
    const hour = +timestamp.slice(9, 11);
    const minute = +timestamp.slice(11, 13);
    const second = +timestamp.slice(13, 15);
    return new Date(year, month, day, hour, minute, second);
  }

  async carregarFotosPorDispositivo(dispositivoId: string) {

    const storage = getStorage();
    const clienteId = this.auth.getClienteId();
    const dbRef = ref(this.db);

    this.fotosCapturadas = [];
    this.fotoSelecionada = 0;

    if (!dispositivoId) {
      console.warn('Dispositivo não selecionado');
      return;
    }

    try {
      const fotosRef = storageRef(storage, `clientes/${clienteId}/dispositivos/${dispositivoId}/fotos`);
      const listResult = await listAll(fotosRef);

      if (listResult.items.length === 0) {
        console.warn('Nenhuma foto encontrada neste dispositivo.');
      }

      const arquivosOrdenados = listResult.items
        .sort((a, b) => b.name.localeCompare(a.name));

      const fotosAtuais = arquivosOrdenados.slice(0, this.limiteFotos - 1);

      for (const item of fotosAtuais) {
        const url = await getDownloadURL(item);
        const fileName = item.name.replace('.jpg', '');
        const dateObj = this.parseTimestampToDate(fileName);
        const dadosPath = `clientes/${clienteId}/dispositivos/${dispositivoId}/dados/${fileName}`;
        const dadosSnap = await get(child(dbRef, dadosPath));
        const dados = dadosSnap.exists() ? dadosSnap.val() : null;

        console.log('Foto carregada:', {
          url,
          nomeArquivo: fileName,
          dados,
          dataConvertida: dateObj
        });

        this.fotosCapturadas.push({ url, dados, dateObj: dateObj });
      }
    } catch (error) {
      console.error('Erro ao carregar fotos:', error);
    }
  }

  async carregarDispositivosPre() {
    const dbRef = ref(this.db);
    const role = this.auth.getRole();
    const clienteId = this.auth.getClienteId();

    try {
      const snapshotPre = await get(child(dbRef, 'mapaSn'));
      if (snapshotPre.exists()) {
        const dispositivos = snapshotPre.val();
        const dispositivoIds = Object.keys(dispositivos);

        const dispositivosFiltrados = role === 'root'
          ? dispositivoIds
          : dispositivoIds.filter(id => dispositivos[id].clienteId === clienteId);

        this.contabilizarDadosPre(dispositivos, dispositivosFiltrados);
      }
    } catch (error) {
      console.error('Erro ao carregar dispositivos pré-cadastrados:', error);
    }
  }

  contabilizarDadosPre(dispositivos: any, dispositivoIds: string[]) {
    this.totalDispositivosPre = dispositivoIds.length;
  }

  contabilizarDados(clientes: any, clienteIds: string[]) {
    let totalUsuarios = 0;
    let totalDispositivos = 0;
    let dispositivosOffline = 0;
    this.dispositivosFiltrados = [];

    clienteIds.forEach((clienteId) => {
      const cliente = clientes[clienteId];
      const dispositivos = cliente.dispositivos ? Object.keys(cliente.dispositivos) : [];
      totalDispositivos += dispositivos.length;
      totalUsuarios += cliente.usuarios ? Object.keys(cliente.usuarios).length : 0;
    });

    this.totalUsuarios = totalUsuarios;
    this.totalDispositivos = totalDispositivos;
    this.dispositivosOffline = dispositivosOffline;
    this.createChart();
  }

  async carregarDadosGraficoPorDispositivo(dispositivoId: string) {
    const clienteId = this.auth.getClienteId();
    const caminho = `clientes/${clienteId}/dispositivos/${dispositivoId}/dados`;
    const dbRef = ref(this.db);

    try {
      const snapshot = await get(child(dbRef, caminho));
      if (!snapshot.exists()) {
        console.warn('Nenhum dado encontrado para o dispositivo.');
        return;
      }

      const dados = snapshot.val();
      const entradas = Object.entries(dados) as [string, any][];

      const ultimos = entradas
        .sort((a, b) => b[0].localeCompare(a[0]))
        .slice(0, 10)
        .reverse(); // reverso para mostrar do mais antigo ao mais novo

      const labels = ultimos.map(([timestamp]) => timestamp.slice(9)); // mostra só HHMMSS
      const temperaturas = ultimos.map(([, v]) => v.temperatura ?? 0);
      const umidades = ultimos.map(([, v]) => v.umidade ?? 0);

      if (this.grafico) {
        this.grafico.destroy();
      }

      const ctx = document.getElementById('graficoDispositivo') as HTMLCanvasElement;
      this.grafico = new Chart(ctx, {
        type: 'line',
        data: {
          labels,
          datasets: [
            {
              label: 'Temperatura (°C)',
              data: temperaturas,
              borderColor: 'red',
              tension: 0.3,
              fill: false
            },
            {
              label: 'Umidade (%)',
              data: umidades,
              borderColor: 'blue',
              tension: 0.3,
              fill: false
            }
          ]
        },
        options: {
          responsive: true,
          scales: {
            x: {
              title: { display: true, text: 'Hora' }
            },
            y: {
              title: { display: true, text: 'Valor' },
              beginAtZero: true
            }
          }
        }
      });
    } catch (error) {
      console.error('Erro ao carregar dados do gráfico:', error);
    }
  }


  createChart() {
    const dispositivosDados = this.dispositivosFiltrados.map((dispositivo) => {
      const dados = dispositivo.dados ? Object.keys(dispositivo.dados) : [];
      return dados.length;
    });

    const labels = this.dispositivosFiltrados.map((dispositivo) => dispositivo.nome || dispositivo.id);

    this.lineChartData = {
      labels,
      datasets: [{
        label: 'Quantidade de Dados por Dispositivo',
        data: dispositivosDados,
        borderColor: 'rgb(75, 192, 192)',
        fill: false,
        tension: 0.1
      }]
    };
  }
  get fotoSelecionadaDados() {
    return this.fotosCapturadas.slice(-6)[this.fotoSelecionada]?.dados;
  }

}
