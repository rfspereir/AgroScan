import { Component, OnInit } from '@angular/core';
import { CommonModule } from '@angular/common';
import { RouterModule } from '@angular/router';
import { Topbar } from '../../shared/topbar/topbar';
import { Database, ref, get, child } from '@angular/fire/database';
import { AuthService } from '../../services/auth.service';
import { FormsModule } from '@angular/forms';
import { Chart } from 'chart.js';  // Importando Chart.js

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
  
  // Dados para o gráfico
  public lineChartData: any;
  public lineChartOptions: any = {
    responsive: true,
    scales: {
      x: {
        title: {
          display: true,
          text: 'Ano'
        }
      },
      y: {
        title: {
          display: true,
          text: '%'
        },
        min: 60,
        max: 100
      }
    }
  };

  public lineChartType: string = 'line';  // Tipo de gráfico (linha)

  constructor(private db: Database, public auth: AuthService) {}

  ngOnInit(): void {
    if (this.auth.getRole() !== 'root') {
      this.carregarDados();
      this.createChart(); 
    }
    if (this.auth.getRole() === 'root') {
      this.carregarDados();
      
    }
  }

  async carregarDados() {
    const dbRef = ref(this.db);
    const role = this.auth.getRole();
    const clienteId = this.auth.getClienteId();

    try {
      if (role === 'root') {
        // Carregar todos os clientes para usuários root
        const snapshot = await get(child(dbRef, 'clientes'));
        if (snapshot.exists()) {
          const clientes = snapshot.val();
          const clienteIds = Object.keys(clientes);
          this.totalClientes = clienteIds.length;
          this.contabilizarDados(clientes, clienteIds);
          this.carregarDispositivosPre();
          
        }
      } else {
        // Carregar apenas o cliente logado
        if (!clienteId) {
          console.error('clienteId não encontrado!');
          return;
        }
        const snapshot = await get(child(dbRef, `clientes/${clienteId}`));
        if (snapshot.exists()) {
          const cliente = snapshot.val();
          this.totalClientes = 1;
          this.contabilizarDados({ [clienteId]: cliente }, [clienteId]);
          this.carregarDispositivosPre();
        }
      }
    } catch (error) {
      console.error('Erro ao carregar dados do dashboard:', error);
    }
  }

  async carregarDispositivosPre() {
    const dbRef = ref(this.db);
    const role = this.auth.getRole(); 
    const clienteId = this.auth.getClienteId(); 

    try {
      if (role === 'root') {
        const snapshotPre = await get(child(dbRef, 'mapaSn'));
        if (snapshotPre.exists()) {
          const dispositivos = snapshotPre.val();
          const dispositivoIds = Object.keys(dispositivos);
          this.contabilizarDadosPre(dispositivos, dispositivoIds);
          console.log('Dispositivos pré-cadastrados:', dispositivoIds);
        }
      } else {
        if (!clienteId) {
          console.error('clienteId não encontrado!');
          return;
        }

        const snapshotPre = await get(child(dbRef, 'mapaSn'));
        if (snapshotPre.exists()) {
          const dispositivos = snapshotPre.val();
          const dispositivoIds = Object.keys(dispositivos);


          console.log('Dispositivos pré-cadastrados:', dispositivoIds);  
          // Filtrando os dispositivos para o clienteId logado
          const dispositivosFiltrados = dispositivoIds.filter((dispositivoId) => {
            return dispositivos[dispositivoId].clienteId === clienteId;
          });

          // Contabiliza os dispositivos filtrados
          this.contabilizarDadosPre(dispositivos, dispositivosFiltrados);
        }
      }
    } catch (error) {
      console.error('Erro ao carregar dispositivos pré-cadastrados:', error);
    }
  }

  contabilizarDadosPre(dispositivos: any, dispositivoIds: string[]) {
    let totalDispositivosPre = 0;

    dispositivoIds.forEach((dispositivoId) => {
      const dispositivo = dispositivos[dispositivoId];

      console.log(dispositivo);  

      totalDispositivosPre++;
    });

    console.log('Total de dispositivos pré-cadastrados:', totalDispositivosPre);
    this.totalDispositivosPre = totalDispositivosPre;
  }


  contabilizarDados(clientes: any, clienteIds: string[]) {
    let totalUsuarios = 0;
    let totalDispositivos = 0;
    let dispositivosOffline = 0;

    clienteIds.forEach((clienteId) => {
      const cliente = clientes[clienteId];
      const dispositivos = cliente.dispositivos ? Object.keys(cliente.dispositivos) : [];

      totalDispositivos += dispositivos.length;
      dispositivos.forEach((dispositivoId) => {
        const dispositivo = cliente.dispositivos[dispositivoId];
        
        // Verificando status do dispositivo
        if (dispositivo.status === 'offline') {
          dispositivosOffline++;
        }

        // Filtros por dispositivo e período
        if (this.periodoInicio && this.periodoFim) {
          const dados = dispositivo.dados ? Object.keys(dispositivo.dados) : [];
          dados.forEach((timestamp) => {
            const dataTimestamp = timestamp;  // Considerando o timestamp do dado
            if (dataTimestamp >= this.periodoInicio && dataTimestamp <= this.periodoFim) {
              // Se o dado estiver dentro do período, incluir o dispositivo
              this.dispositivosFiltrados.push(dispositivo);
            }
          });
        } else {
          // Se o período não estiver filtrado, incluir todos os dispositivos
          this.dispositivosFiltrados.push(dispositivo);
        }
      });

      totalUsuarios += cliente.usuarios ? Object.keys(cliente.usuarios).length : 0;
    });

    this.totalUsuarios = totalUsuarios;
    this.totalDispositivos = totalDispositivos;
    this.dispositivosOffline = dispositivosOffline;
    
    // Após carregar os dados, gerar o gráfico
    this.createChart();
  }

  // Definir filtros de período
  definirPeriodo(inicio: string, fim: string) {
    this.periodoInicio = inicio;
    this.periodoFim = fim;
    this.dispositivosFiltrados = [];  // Limpa os dispositivos filtrados
    this.carregarDados();  // Recarrega os dados com o filtro aplicado
  }

  // Seleção de dispositivo (para filtro específico)
  selecionarDispositivo(dispositivoId: string) {
    this.dispositivoSelecionado = dispositivoId;
    this.carregarDados();  // Recarregar dados aplicando o filtro de dispositivo
  }

  // Função para criar o gráfico
  createChart() {
    const dispositivosDados = this.dispositivosFiltrados.map((dispositivo) => {
      const dados = dispositivo.dados ? Object.keys(dispositivo.dados) : [];
      return dados.length;  // Exemplo: contar os dados de cada dispositivo
    });

    const labels = this.dispositivosFiltrados.map((dispositivo) => dispositivo.nome);  // Use o nome do dispositivo como label

    // Configuração do gráfico de linha
    this.lineChartData = {
      labels: labels,  // Labels são os nomes dos dispositivos
      datasets: [{
        label: 'Quantidade de Dados por Dispositivo',
        data: dispositivosDados,  // Quantidade de dados para cada dispositivo
        borderColor: 'rgb(75, 192, 192)',  // Cor da linha
        fill: false,
        tension: 0.1
      }]
    };
  }
}
