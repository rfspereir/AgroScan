import { Component, OnInit } from '@angular/core';
import { CommonModule } from '@angular/common';
import { RouterModule } from '@angular/router';
import { Topbar } from '../../shared/topbar/topbar';
import { Database, ref, get, child, set, remove, update, push } from '@angular/fire/database';
import { FormsModule } from '@angular/forms';
import { Html5Qrcode } from 'html5-qrcode';

@Component({
  selector: 'app-dispositivos',
  standalone: true,
  imports: [CommonModule, RouterModule, Topbar, FormsModule],
  templateUrl: './dispositivos.html',
  styleUrls: ['./dispositivos.css']
})
export class Dispositivos implements OnInit {
  dispositivos: any[] = [];
  clientes: any[] = [];
  clienteSelecionado = '';

  novoDispositivoId = '';
  novoDispositivoMac = '';
  novoDispositivoNome = '';
  novoDispositivoDescricao = '';

  constructor(private db: Database) {}

  ngOnInit(): void {
    this.carregarClientes();
    this.carregarDispositivos();
  }

  async carregarClientes() {
    const dbRef = ref(this.db);
    try {
      const snapshot = await get(child(dbRef, 'clientes'));
      if (snapshot.exists()) {
        const dados = snapshot.val();
        this.clientes = Object.keys(dados).map((key) => ({
          id: key,
          nome: dados[key].nome
        }));
      }
    } catch (error) {
      console.error('Erro ao carregar clientes:', error);
    }
  }

  async carregarDispositivos() {
    if (!this.clienteSelecionado) {
      this.dispositivos = [];
      return;
    }

    const dbRef = ref(this.db);
    try {
      const snapshot = await get(
        child(dbRef, `clientes/${this.clienteSelecionado}/dispositivos`)
      );
      if (snapshot.exists()) {
        const dados = snapshot.val();
        this.dispositivos = Object.keys(dados).map((key) => ({
          id: key,
          ...dados[key]
        }));
      } else {
        this.dispositivos = [];
      }
    } catch (error) {
      console.error('Erro ao carregar dispositivos:', error);
    }
  }

  async adicionarDispositivo() {
    if (
      !this.novoDispositivoNome.trim() ||
      !this.clienteSelecionado
    )
      return;

    const dispositivoRef = push(ref(this.db, `clientes/${this.clienteSelecionado}/dispositivos`));

    await set(dispositivoRef, {
      sn: this.novoDispositivoId.trim() || '',
      mac: this.novoDispositivoMac.trim() || '',
      nome: this.novoDispositivoNome.trim(),
      descricao: this.novoDispositivoDescricao.trim()
    });

    this.novoDispositivoId = '';
    this.novoDispositivoMac = '';
    this.novoDispositivoNome = '';
    this.novoDispositivoDescricao = '';

    this.carregarDispositivos();
  }

  async removerDispositivo(id: string) {
    await remove(
      ref(this.db, `clientes/${this.clienteSelecionado}/dispositivos/${id}`)
    );
    this.carregarDispositivos();
  }

  async editarDispositivo(dispositivo: any) {
    if (
      !dispositivo.novoNome.trim() ||
      !dispositivo.novoDescricao.trim()
    )
      return;

    const atualizacao = {
      sn: dispositivo.novoSn.trim() || '',
      mac: dispositivo.novoMac.trim() || '',
      nome: dispositivo.novoNome.trim(),
      descricao: dispositivo.novoDescricao.trim()
    };

    const dbRef = ref(
      this.db,
      `clientes/${this.clienteSelecionado}/dispositivos/${dispositivo.id}`
    );
    await update(dbRef, atualizacao);

    dispositivo.editando = false;
    this.carregarDispositivos();
  }

  habilitarEdicao(dispositivo: any) {
    dispositivo.editando = true;
    dispositivo.novoSn = dispositivo.sn || '';
    dispositivo.novoMac = dispositivo.mac || '';
    dispositivo.novoNome = dispositivo.nome;
    dispositivo.novoDescricao = dispositivo.descricao;
  }

  cancelarEdicao(dispositivo: any) {
    dispositivo.editando = false;
  }

  lerQrCamera() {
    const qrCodeScanner = new Html5Qrcode('scanner');

    qrCodeScanner.start(
      { facingMode: 'environment' },
      { fps: 10, qrbox: 250 },
      (decodedText) => {
        console.log('QR lido:', decodedText);
        this.preencherDadosDoQr(decodedText);
        qrCodeScanner.stop();
      },
      (errorMessage) => {
        console.warn('Erro:', errorMessage);
      }
    );
  }

  lerQrImagem(event: any) {
    const file = event.target.files[0];
    const qrCodeScanner = new Html5Qrcode('scanner');

    qrCodeScanner.scanFile(file, true)
      .then((decodedText) => {
        console.log('QR lido:', decodedText);
        this.preencherDadosDoQr(decodedText);
      })
      .catch(err => {
        console.warn('Erro ao ler imagem:', err);
      });
  }

  preencherDadosDoQr(decodedText: string) {
    try {
      const dados = JSON.parse(decodedText);
      this.novoDispositivoId = dados.sn || '';
      this.novoDispositivoMac = dados.mac || '';
      this.novoDispositivoNome = dados.nome || '';
      this.novoDispositivoDescricao = dados.descricao || '';
    } catch {
      this.novoDispositivoId = decodedText;
    }
  }
}
