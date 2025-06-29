import { Component, OnInit } from '@angular/core';
import { CommonModule } from '@angular/common';
import { RouterModule } from '@angular/router';
import { Topbar } from '../../shared/topbar/topbar';
import { Database, ref, get, set, child, remove } from '@angular/fire/database';
import { FormsModule } from '@angular/forms';
import { Functions, httpsCallable } from '@angular/fire/functions';
import { AuthService } from '../../services/auth.service';


@Component({
  selector: 'app-usuarios',
  standalone: true,
  imports: [CommonModule, RouterModule, Topbar, FormsModule],
  templateUrl: './usuarios.html',
  styleUrls: ['./usuarios.css']
})
export class Usuarios implements OnInit {
  usuarios: any[] = [];
  clientes: any[] = [];
  clienteSelecionado = '';

  novoUsuarioNome = '';
  novoUsuarioEmail = '';
  novoUsuarioRole = 'operador';
  novaSenha = '';

  constructor(private db: Database, private functions: Functions, public auth: AuthService) {}

ngOnInit(): void {
  const role = this.auth.getRole();
  const clienteId = this.auth.getClienteId();

  if (role === 'root') {
    this.carregarTodosClientes();
  } else {
    this.clienteSelecionado = clienteId || '';
    this.carregarUsuarios();
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
          nome: dados[key].nome
        }));
      }
    } catch (error) {
      console.error('Erro ao carregar clientes:', error);
    }
  }

  async carregarUsuarios() {
    if (!this.clienteSelecionado) {
      this.usuarios = [];
      return;
    }

    const dbRef = ref(this.db);
    try {
      const snapshot = await get(
        child(dbRef, `clientes/${this.clienteSelecionado}/usuarios`)
      );
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
      console.error('this.clienteSelecionado:', this.clienteSelecionado);
    }
  }

  gerarSenha() {
    const caracteres = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%&*';
    this.novaSenha = Array.from({ length: 12 }, () =>
      caracteres.charAt(Math.floor(Math.random() * caracteres.length))
    ).join('');
  }

  async criarUsuarioAuth() {
  if (
    !this.novoUsuarioNome.trim() ||
    !this.novoUsuarioEmail.trim() ||
    !this.clienteSelecionado ||
    !this.novaSenha.trim()
  ) {
    console.error('Todos os campos são obrigatórios');
    return;
  }

  const createUser = httpsCallable(this.functions, 'createUser');

  try {
    const result = await createUser({
      email: this.novoUsuarioEmail.trim(),
      password: this.novaSenha.trim(),
      nome: this.novoUsuarioNome.trim(),
      clienteId: this.clienteSelecionado,
      role: this.novoUsuarioRole
    });

    console.log('Usuário criado com sucesso:', result);

    this.novoUsuarioNome = '';
    this.novoUsuarioEmail = '';
    this.novoUsuarioRole = 'operador';
    this.novaSenha = '';
    this.carregarUsuarios();
    } catch (error) {
      console.error('Erro ao criar usuário:', error);
    }
  }

  async excluirUsuarioAuth(usuario: any) {
    const deleteUser = httpsCallable(this.functions, 'deleteUser');

    try {
      await deleteUser({
        uid: usuario.id,
        clienteId: this.clienteSelecionado
      });

      console.log('Usuário excluído com sucesso');
      this.carregarUsuarios();
    } catch (error) {
      console.error('Erro ao excluir usuário:', error);
    }
  }


  async editarUsuarioAuth(usuario: any) {
  const editUser = httpsCallable(this.functions, 'editUser');

  try {
    await editUser({
      uid: usuario.id,
      clienteId: this.clienteSelecionado,
      nome: usuario.novoNome.trim(),
      role: usuario.novoRole.trim()
    });

    console.log('Usuário atualizado com sucesso');
    usuario.editando = false;
    this.carregarUsuarios();
  } catch (error) {
    console.error('Erro ao editar usuário:', error);
  }
}

  habilitarEdicao(usuario: any) {
    usuario.editando = true;
    usuario.novoNome = usuario.nome;
    usuario.novoRole = usuario.role;
  }

  cancelarEdicao(usuario: any) {
    usuario.editando = false;
  }

  onNomeChange() {
    this.gerarSenha();
  }
}
