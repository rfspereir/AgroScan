import { Component, OnInit } from '@angular/core';
import { CommonModule } from '@angular/common';
import { RouterModule } from '@angular/router';
import { Topbar } from '../../shared/topbar/topbar';
import { Database, ref, get, child, set, remove} from '@angular/fire/database';
import { FormsModule } from '@angular/forms';
import { AuthService } from '../../services/auth.service';

@Component({
  selector: 'app-pre-cadastro-disp',
  standalone: true,
  imports: [CommonModule, RouterModule, Topbar, FormsModule],
  templateUrl: './pre-cadastro-disp.html',
  styleUrls: ['./pre-cadastro-disp.css']
})
export class PreCadastroDisp implements OnInit {
  dispositivos: any[] = [];
  clientes: any[] = [];
  clienteSelecionado = '';
  novoDispositivoSn = '';

  constructor(private db: Database, public auth: AuthService) {}

  ngOnInit(): void {
    const role = this.auth.getRole();
    const clienteId = this.auth.getClienteId();
    if (role === 'root') {
      this.carregarTodosClientes();
  } else {
      this.clienteSelecionado = clienteId || '';
      this.carregarDispositivos();
  }
  }

  async carregarTodosClientes() {
    const dbRef = ref(this.db);
    try {
      const snapshot = await get(child(dbRef, 'clientes'));
      if (snapshot.exists()) {
        const dados = snapshot.val();
        this.clientes = Object.keys(dados).map((key) => ({
          id: key,
          nome: dados[key].nome || key
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
      const snapshot = await get(child(dbRef, `mapaSn`));
      if (snapshot.exists()) {
        const dados = snapshot.val();
        this.dispositivos = Object.keys(dados)
          .filter((key) => dados[key].clienteId === this.clienteSelecionado)
          .map((key) => ({
            id: key,
            editando: false
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
      !this.clienteSelecionado ||
      !this.novoDispositivoSn.trim()
    ) {
      alert('Preencha Cliente e SN.');
      return;
    }

    const sn = this.novoDispositivoSn.trim();
    const dispositivoRef = ref(this.db, `mapaSn/${sn}`);

    await set(dispositivoRef, {
      clienteId: this.clienteSelecionado,
    });

    this.novoDispositivoSn = '';
    await this.carregarDispositivos();
  }

  async removerDispositivo(id: string) {
    await remove(ref(this.db, `mapaSn/${id}`));
    await this.carregarDispositivos();
  }
}
