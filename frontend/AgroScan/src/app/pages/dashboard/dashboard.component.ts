import { Component, OnInit } from '@angular/core';
import { Database, ref, onValue } from '@angular/fire/database';
import { Storage, ref as storageRef, listAll, getDownloadURL } from '@angular/fire/storage';

@Component({
  selector: 'app-dashboard',
  templateUrl: './dashboard.component.html',
  styleUrls: ['./dashboard.component.css']
})
export class DashboardComponent implements OnInit {
  umidade = 0;
  variacaoUmidade = 0;
  temperatura = 0;
  temperaturaFahrenheit = 0;
  coberturaNuvem = 0;
  dataSelecionada = this.getDataHoje();
  fotos: string[] = [];

  dadosPraga: number[] = [];
  labelsPraga: string[] = [];
  chartOptions = { responsive: true };

  constructor(private db: Database, private storage: Storage) {}

  ngOnInit(): void {
    this.carregarDadosMeteorologicos();
    this.carregarGraficoPraga();
    this.carregarFotos();
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
      this.dadosPraga = val.map((p: any) => p.valor);
      this.labelsPraga = val.map((p: any) => p.ano);
    });
  }

  async carregarFotos() {
    const pasta = `${this.dataSelecionada}/`;
    const fotosRef = storageRef(this.storage, pasta);
    const result = await listAll(fotosRef);
    this.fotos = await Promise.all(result.items.map(item => getDownloadURL(item)));
  }

  getDataHoje(): string {
    return new Date().toISOString().split('T')[0];
  }
}
