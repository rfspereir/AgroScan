import { Component } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';

import { provideDatabase, getDatabase, Database, ref, onValue } from '@angular/fire/database';
import { provideStorage, getStorage, Storage, ref as storageRef, listAll, getDownloadURL } from '@angular/fire/storage';
import { NgChartsModule } from 'ng2-charts';





@Component({
  standalone: true,
  selector: 'app-dashboard',
  templateUrl: './dashboard.component.html',
  styleUrls: ['./dashboard.component.css'],
  imports: [CommonModule, FormsModule, NgChartsModule ]
})
export class DashboardComponent {
  umidade = 0;
  variacaoUmidade = 0;

  temperatura = 0;
  temperaturaFahrenheit = 0;
  coberturaNuvem = 0;

  dataSelecionada = this.getDataHoje();
  fotos: string[] = [];

  labelsPraga: string[] = [];
  dadosPraga: number[] = [];

  chartData = {
    labels: [] as string[],
    datasets: [
      {
        label: 'Praga X',
        data: [] as number[],
        tension: 0.3,
        borderColor: 'green',
        fill: false
      }
    ]
  };

  chartOptions = {
    responsive: true,
    plugins: {
      legend: {
        display: true
      }
    }
  };

  constructor(private db: Database, private storage: Storage) {
    this.carregarDadosMeteorologicos();
    this.carregarGraficoPraga();
    this.carregarFotos();
  }

  getDataHoje(): string {
    return new Date().toISOString().split('T')[0];
  }

  
  carregarDadosMeteorologicos() {
    onValue(ref(this.db, 'dados/umidade'), snapshot => {
      const val = snapshot.val();
      this.umidade = val.atual;
      this.variacaoUmidade = val.variacao;
    });

    onValue(ref(this.db, 'dados/temperatura'), snapshot => {
      const val = snapshot.val();
      this.temperatura = val.celsius;
      this.temperaturaFahrenheit = val.fahrenheit;
      this.coberturaNuvem = val.nuvem;
    });
  }

  carregarGraficoPraga() {
    onValue(ref(this.db, 'dados/pragaX'), snapshot => {
      const val = snapshot.val();
      this.labelsPraga = val.map((p: any) => p.ano);
      this.dadosPraga = val.map((p: any) => p.valor);
      this.chartData.labels = this.labelsPraga;
      this.chartData.datasets[0].data = this.dadosPraga;
    });
  }

  async carregarFotos() {
    const pasta = `${this.dataSelecionada}/`;
    const fotosRef = storageRef(this.storage, pasta);
    const result = await listAll(fotosRef);
    this.fotos = await Promise.all(result.items.map(item => getDownloadURL(item)));
  }
}

