import { Component, OnInit } from '@angular/core';
import { CommonModule } from '@angular/common';
import { RouterModule } from '@angular/router';
import { Topbar } from '../../shared/topbar/topbar';
import { Database, ref, get, child, set, remove, push, update } from '@angular/fire/database';
import { FormsModule } from '@angular/forms';

@Component({
  selector: 'app-clientes',
  standalone: true,
  imports: [CommonModule, RouterModule, Topbar, FormsModule],
  templateUrl: './clientes.html',
  styleUrls: ['./clientes.css']
})
export class Clientes implements OnInit {
  clientes: any[] = [];
  novoClienteNome = '';

  constructor(private db: Database) {}

  ngOnInit(): void {
    this.carregarClientes();
  }

async carregarClientes() {
  const dbRef = ref(this.db);
  try {
    const snapshot = await get(child(dbRef, 'clientes'));
    if (snapshot.exists()) {
      const dados = snapshot.val();
      this.clientes = Object.keys(dados).map((key) => ({
        id: key,
        ...dados[key]
      }));
    } else {
      this.clientes = [];
    }
  } catch (error) {
    console.error('Erro ao carregar clientes:', error);
  }
}
  async adicionarCliente() {
    if (!this.novoClienteNome.trim()) return;

    const novoRef = push(ref(this.db, 'clientes'));
    await set(novoRef, {
      nome: this.novoClienteNome.trim()
    });

    this.novoClienteNome = '';
    this.carregarClientes();
  }

  async removerCliente(id: string) {
    await remove(ref(this.db, `clientes/${id}`));
    this.carregarClientes();
  }

  async editarCliente(cliente: any) {
    if (!cliente.novoNome.trim()) return;

    const atualizacao = { nome: cliente.novoNome.trim() };

    await update(ref(this.db, `clientes/${cliente.id}`), atualizacao);

  cliente.editando = false;
  this.carregarClientes();
  } 

  habilitarEdicao(cliente: any) {
    cliente.editando = true;
    cliente.novoNome = cliente.nome;
  }

  cancelarEdicao(cliente: any) {
    cliente.editando = false;
  }

}
