import { Component, OnInit } from '@angular/core';
import { CommonModule } from '@angular/common';
import { RouterModule } from '@angular/router';
import { Topbar } from '../../shared/topbar/topbar';
import { Database, ref, get, child, set, remove, push, update } from '@angular/fire/database';
import { FormsModule } from '@angular/forms';

@Component({
  selector: 'app-usuarios',
  standalone: true,
  imports: [CommonModule, RouterModule, Topbar, FormsModule],
  templateUrl: './usuarios.html',
  styleUrls: ['./usuarios.css']
})
export class Usuarios implements OnInit {
  usuarios: any[] = [];
  novoUsuarioNome = '';
  novoUsuarioEmail = '';
  novoUsuarioRole = 'operador';

  clienteId = 'cliente_id_1'; // 🔥 ← Depois podemos pegar isso dinamicamente do auth

  constructor(private db: Database) {}

  ngOnInit(): void {
    this.carregarUsuarios();
  }

  async carregarUsuarios() {
    const dbRef = ref(this.db);
    try {
      const snapshot = await get(child(dbRef, `clientes/${this.clienteId}/usuarios`));
      if (snapshot.exists()) {
        const dados = snapshot.val();
        this.usuarios = Object.keys(dados).map((key) => ({
          id: key,
          ...dados[key]
        }));
      } else {
        this.usuarios = [];
      }
    } catch (error) {
      console.error('Erro ao carregar usuários:', error);
    }
  }

  async adicionarUsuario() {
    if (!this.novoUsuarioNome.trim() || !this.novoUsuarioEmail.trim()) return;

    const novoRef = push(ref(this.db, `clientes/${this.clienteId}/usuarios`));
    await set(novoRef, {
      nome: this.novoUsuarioNome.trim(),
      email: this.novoUsuarioEmail.trim(),
      role: this.novoUsuarioRole
    });

    this.novoUsuarioNome = '';
    this.novoUsuarioEmail = '';
    this.novoUsuarioRole = 'operador';
    this.carregarUsuarios();
  }

  async removerUsuario(id: string) {
    await remove(ref(this.db, `clientes/${this.clienteId}/usuarios/${id}`));
    this.carregarUsuarios();
  }

  async editarUsuario(usuario: any) {
    if (!usuario.novoNome.trim() || !usuario.novoRole.trim()) return;

    const atualizacao = {
      nome: usuario.novoNome.trim(),
      role: usuario.novoRole.trim()
    };

    await update(ref(this.db, `clientes/${this.clienteId}/usuarios/${usuario.id}`), atualizacao);

    usuario.editando = false;
    this.carregarUsuarios();
  }

  habilitarEdicao(usuario: any) {
    usuario.editando = true;
    usuario.novoNome = usuario.nome;
    usuario.novoRole = usuario.role;
  }

  cancelarEdicao(usuario: any) {
    usuario.editando = false;
  }
}
