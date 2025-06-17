import { Component, OnInit } from '@angular/core';
import { CommonModule } from '@angular/common';
import { RouterModule } from '@angular/router';
import { Topbar } from '../../shared/topbar/topbar';
import { Database, ref, get, child } from '@angular/fire/database';
import { Auth } from '@angular/fire/auth';

@Component({
  selector: 'app-dashboard-root',
  standalone: true,
  imports: [CommonModule, RouterModule, Topbar],
  templateUrl: './dashboard-root.html',
  styleUrls: ['./dashboard-root.css']
})
export class DashboardRoot implements OnInit {
  totalClientes = 0;
  totalUsuarios = 0;
  totalDispositivos = 0;
  dispositivosOffline = 0;

  constructor(private db: Database, private auth: Auth) {}

  ngOnInit(): void {
  this.carregarDados();
}

  // ngOnInit(): void {
  //   this.verificarClaims();
  //   this.carregarDados();
  // }

  // async verificarClaims() {
  //   const user = await this.auth.currentUser;
  //   if (user) {
  //     const tokenResult = await user.getIdTokenResult(true);
  //     console.log('Claims atuais:', tokenResult.claims);

  //     if (tokenResult.claims['role'] !== 'root') {
  //       console.warn('⚠️ Atenção: Este usuário não é root. As permissões podem estar incorretas.');
  //     }
  //   } else {
  //     console.warn('Nenhum usuário autenticado.');
  //   }
  // }

  async carregarDados() {
    const dbRef = ref(this.db);

    try {
      const snapshot = await get(child(dbRef, 'clientes'));

      if (snapshot.exists()) {
        const clientes = snapshot.val();
        const clienteIds = Object.keys(clientes);
        this.totalClientes = clienteIds.length;

        let totalUsuarios = 0;
        let totalDispositivos = 0;
        let dispositivosOffline = 0;

        clienteIds.forEach((clienteId) => {
          const cliente = clientes[clienteId];

          const usuarios = cliente.usuarios ? Object.keys(cliente.usuarios) : [];
          totalUsuarios += usuarios.length;

          const dispositivos = cliente.dispositivos ? Object.keys(cliente.dispositivos) : [];
          totalDispositivos += dispositivos.length;

          dispositivos.forEach((dispositivoId) => {
            const dispositivo = cliente.dispositivos[dispositivoId];
            if (dispositivo.status === 'offline') {
              dispositivosOffline++;
            }
          });
        });

        this.totalUsuarios = totalUsuarios;
        this.totalDispositivos = totalDispositivos;
        this.dispositivosOffline = dispositivosOffline;

      } else {
        console.log('Nenhum cliente encontrado');
      }
    } catch (error) {
      console.error('Erro ao carregar clientes:', error);
    }
  }
}
