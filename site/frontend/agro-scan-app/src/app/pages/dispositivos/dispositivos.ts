import { Component, OnInit } from '@angular/core';
import { CommonModule } from '@angular/common';
import { RouterModule } from '@angular/router';
import { Topbar } from '../../shared/topbar/topbar';
import { Database, ref, get, child, update} from '@angular/fire/database';
import { FormsModule } from '@angular/forms';
import { FirebaseFunctionsService } from '../../services/firebase-functions.service';
import { AuthService } from '../../services/auth.service';

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

  novoDispositivoNome = '';
  novoDispositivoDescricao = '';

  constructor(private db: Database, private functionsService: FirebaseFunctionsService, public auth: AuthService) {}

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
      const snapshot = await get(
        child(dbRef, `clientes/${this.clienteSelecionado}/dispositivos`)
      );
      if (snapshot.exists()) {
        const dados = snapshot.val();
        this.dispositivos = Object.keys(dados).map((key) => ({
          id: key,
          nome: dados[key].nome || '',
          descricao: dados[key].descricao || '',
          editando: false
        }));
      } else {
        this.dispositivos = [];
      }
    } catch (error) {
      console.error('Erro ao carregar dispositivos:', error);
    }
  }

  async removerDispositivo(id: string) {
    const confirmacao = confirm(`Deseja realmente excluir o dispositivo ${id}?`);
    if (!confirmacao) return;

    try {
      const result = await this.functionsService.deleteDevice(id);
      console.log('Dispositivo removido:', result);
      alert('Dispositivo removido com sucesso.');
      await this.carregarDispositivos();
    } catch (error) {
      console.error('Erro ao remover dispositivo:', error);
      alert('Erro ao remover dispositivo.');
    }
}

  async editarDispositivo(dispositivo: any) {
    if (
      !dispositivo.novoNome.trim()
    ) {
      alert('Nome é obrigatório.');
      return;
    }

    const atualizacao = {
      nome: dispositivo.novoNome.trim(),
      descricao: dispositivo.novoDescricao.trim() || ''
    };

    const dbRef = ref(
      this.db,
      `clientes/${this.clienteSelecionado}/dispositivos/${dispositivo.id}`
    );
    await update(dbRef, atualizacao);

    dispositivo.editando = false;
    await this.carregarDispositivos();
  }

  habilitarEdicao(dispositivo: any) {
    dispositivo.editando = true;
    dispositivo.novoNome = dispositivo.nome;
    dispositivo.novoDescricao = dispositivo.descricao;
  }

  cancelarEdicao(dispositivo: any) {
    dispositivo.editando = false;
  }
}
